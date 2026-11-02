"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { createRequire } = require("node:module");
const { createHash } = require("node:crypto");
const compatibility = require("../client-compatibility.json");
const extensionRoot = path.resolve(__dirname, "..");
const digest = source => createHash("sha256").update(source).digest("hex");

function adaptSource(source, patch) {
  if (digest(source) === patch.adaptedSha256) return source;
  if (digest(source) !== patch.originalSha256) {
    throw new Error(`Unrecognized languageclient source: ${patch.path}; refusing adaptation`);
  }
  const text = source.toString("utf8");
  if (text.split(patch.before).length !== 2) throw new Error("Expected exactly one adaptation site");
  const result = Buffer.from(text.replace(patch.before, patch.after), "utf8");
  if (digest(result) !== patch.adaptedSha256) throw new Error("Adaptation hash mismatch");
  return result;
}

function prepare(checkOnly = false, root = extensionRoot) {
  const packageRoot = path.join(root, "node_modules", compatibility.package);
  const installed = JSON.parse(fs.readFileSync(path.join(packageRoot, "package.json"), "utf8"));
  const minimatch = createRequire(path.join(packageRoot, "package.json"))("minimatch/package.json");
  if (installed.version !== compatibility.version || minimatch.version !== compatibility.minimatchVersion) {
    throw new Error("Client compatibility versions changed; review adaptation before proceeding");
  }
  // Validate every input before writing either file. Never patch an unknown or
  // partially upgraded dependency tree; npm ci --ignore-scripts remains intact.
  const pending = compatibility.patches.map(patch => {
    const filename = path.join(packageRoot, patch.path);
    const source = fs.readFileSync(filename);
    const adapted = adaptSource(source, patch);
    if (checkOnly && !source.equals(adapted)) throw new Error(`Unprepared client: ${patch.path}`);
    return { filename, source, adapted };
  });
  for (const { filename, source, adapted } of pending) {
    if (!source.equals(adapted)) fs.writeFileSync(filename, adapted);
  }
}

if (require.main === module) prepare(process.argv.includes("--check"));
module.exports = { adaptSource, prepare };
