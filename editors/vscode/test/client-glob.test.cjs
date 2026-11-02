"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { createRequire } = require("node:module");
const test = require("node:test");
const clientRoot = path.resolve(__dirname, "../node_modules/vscode-languageclient");

// Execute the installed client, replacing only the editor host API. Glob matching
// and protocol dependencies are real: the extension's injected-client tests do
// not exercise this compatibility boundary.
function loadClient(relative, vscode) {
  const filename = path.join(clientRoot, relative);
  const nativeRequire = createRequire(filename);
  const module = { exports: {} };
  const run = vm.runInThisContext(
    `(function (require, module, exports) {${fs.readFileSync(filename, "utf8")}\n})`,
    { filename },
  );
  run((name) => name === "vscode" ? vscode : nativeRequire(name), module, module.exports);
  return module.exports;
}

function uri(fsPath, scheme = "file") {
  return { fsPath, path: fsPath, scheme };
}

const vscode = {
  Uri: {
    parse(value) {
      const base = value.replace(/^file:\/\//, "");
      return { ...uri(base), with({ path: next }) { return uri(next); } };
    },
  },
  FileType: { File: 1, Directory: 2 },
  workspace: {
    onDidCreateFiles() { return { dispose() {} }; },
    fs: { async stat(resource) { return { type: resource.fsPath.endsWith("/folder") ? 2 : 1 }; } },
  },
};

test("installed client matches string and relative globs without removed makeRe", () => {
  const { matchGlobPattern: match } = loadClient("lib/common/utils/globPattern.js", vscode);
  assert.equal(match("**/*.st", uri("/work/main.st")), true);
  assert.equal(match("**/*.st", uri("/work/main.cpp")), false);
  assert.equal(match("**/*.{st,ST}", uri("/work/main.ST")), true);
  assert.equal(match("C:\\work\\*.st", uri("C:/work/main.st")), true);
  assert.equal(match({ baseUri: "file:///work", pattern: "**/*.st" }, uri("/work/a/main.st")), true);
  assert.equal(match({ baseUri: "file:///work", pattern: "**/*.st" }, uri("/other/main.st")), false);
  assert.equal(match("C:/work/*.st", uri("C:\\work\\main.st")), process.platform === "win32");
  assert.equal(match("", uri("")), false);
  assert.equal(match("#comment", uri("#comment")), false);
});

test("installed file-operation client registers and filters real globs", async () => {
  const { DidCreateFilesFeature } = loadClient("lib/common/fileOperations.js", vscode);
  const client = { code2ProtocolConverter: { asDidCreateFilesParams: x => x } };
  const feature = new DidCreateFilesFeature(client);
  feature.register({ id: "st", registerOptions: { filters: [{ scheme: "file", pattern: {
    glob: "**/*.{st,iec}", options: { ignoreCase: true }, matches: "file",
  } }] } });
  const files = [uri("C:\\work\\main.ST"), uri("/work/.hidden.iec"), uri("/work/main.cpp"), uri("/work/main.st", "untitled")];
  const filtered = await feature.filter({ files }, x => x);
  assert.deepEqual(filtered.files, files.slice(0, 2));
  assert.throws(() => feature.register({ id: "empty", registerOptions: { filters: [{ pattern: { glob: "" } }] } }), /Invalid pattern/);
  assert.throws(() => feature.register({ id: "comment", registerOptions: { filters: [{ pattern: { glob: "#comment" } }] } }), /Invalid pattern/);
  feature.unregister("st");
  assert.equal((await feature.filter({ files }, x => x)).files.length, 0);
  feature.register({ id: "folder", registerOptions: { filters: [{ pattern: {
    glob: "/work/folder/", matches: "folder",
  } }] } });
  const folder = uri("/work/folder");
  assert.deepEqual((await feature.filter({ files: [folder] }, x => x)).files, [folder]);
  feature.clear();
});

test("client adaptation is idempotent and rejects unknown source", () => {
  const { adaptSource, prepare } = require("../scripts/prepare-languageclient.cjs");
  const compatibility = require("../client-compatibility.json");
  for (const patch of compatibility.patches) {
    const adapted = fs.readFileSync(path.join(clientRoot, patch.path));
    const original = Buffer.from(adapted.toString("utf8").replace(patch.after, patch.before));
    assert.deepEqual(adaptSource(original, patch), adapted);
    assert.deepEqual(adaptSource(adapted, patch), adapted);
    assert.throws(() => adaptSource(Buffer.concat([original, Buffer.from("\n// unknown")]), patch), /Unrecognized/);
  }
  assert.doesNotThrow(() => prepare(true));
});

test("client preparation fails closed before any write on invalid tree", () => {
  const os = require("node:os");
  const { prepare } = require("../scripts/prepare-languageclient.cjs");
  const compatibility = require("../client-compatibility.json");
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "plcopen-client-test-"));
  try {
    const target = path.join(root, "node_modules/vscode-languageclient");
    const files = compatibility.patches.map(patch => {
      const filename = path.join(target, patch.path);
      fs.mkdirSync(path.dirname(filename), { recursive: true });
      const original = fs.readFileSync(path.join(clientRoot, patch.path), "utf8").replace(patch.after, patch.before);
      fs.writeFileSync(filename, original);
      return { filename, original };
    });
    fs.writeFileSync(path.join(target, "package.json"), JSON.stringify({ version: compatibility.version }));
    const matcher = path.join(target, "node_modules/minimatch");
    fs.mkdirSync(matcher, { recursive: true });
    fs.writeFileSync(path.join(matcher, "package.json"), JSON.stringify({ version: compatibility.minimatchVersion }));
    assert.throws(() => prepare(true, root), /Unprepared/);
    assert.equal(fs.readFileSync(files[0].filename, "utf8"), files[0].original);
    fs.appendFileSync(files[1].filename, "\n// unexpected change");
    assert.throws(() => prepare(false, root), /Unrecognized/);
    assert.equal(fs.readFileSync(files[0].filename, "utf8"), files[0].original);
    fs.writeFileSync(path.join(target, "package.json"), JSON.stringify({ version: "0.0.0" }));
    assert.throws(() => prepare(false, root), /versions changed/);
    assert.equal(fs.readFileSync(files[0].filename, "utf8"), files[0].original);
  } finally {
    // Only remove the isolated directory created and owned by this test.
    fs.rmSync(root, { recursive: true });
  }
});
