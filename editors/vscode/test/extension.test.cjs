const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const extensionRoot = path.resolve(__dirname, "..");

function makeHarness({ trusted = true, pythonPath = "python", startError } = {}) {
  const clients = [];
  const errors = [];
  const output = [];
  const trustListeners = [];

  class LanguageClient {
    constructor(id, name, serverOptions, clientOptions) {
      this.id = id;
      this.name = name;
      this.serverOptions = serverOptions;
      this.clientOptions = clientOptions;
      this.startCalls = 0;
      this.stopCalls = 0;
      clients.push(this);
    }

    async start() {
      this.startCalls += 1;
      if (startError) {
        throw startError;
      }
    }

    async stop() {
      this.stopCalls += 1;
    }
  }

  const vscode = {
    workspace: {
      isTrusted: trusted,
      getConfiguration(section) {
        assert.equal(section, "plcopenSt");
        return {
          get(key, fallback) {
            assert.equal(key, "pythonPath");
            return pythonPath || fallback;
          },
        };
      },
      onDidGrantWorkspaceTrust(listener) {
        trustListeners.push(listener);
        return { dispose() {} };
      },
    },
    window: {
      createOutputChannel(name) {
        assert.equal(name, "PLCopen ST");
        return {
          appendLine(line) {
            output.push(line);
          },
          dispose() {},
        };
      },
      async showErrorMessage(message) {
        errors.push(message);
      },
    },
  };

  return {
    clients,
    errors,
    languageClient: {
      LanguageClient,
      TransportKind: { stdio: "stdio" },
    },
    output,
    trustListeners,
    vscode,
  };
}

test("manifest fixes scope, dependencies, and trust boundary", () => {
  const manifest = JSON.parse(
    fs.readFileSync(path.join(extensionRoot, "package.json"), "utf8"),
  );

  assert.deepEqual(manifest.dependencies, {
    "vscode-languageclient": "10.1.0",
  });
  assert.deepEqual(manifest.devDependencies, {
    "@vscode/vsce": "3.9.2",
  });
  assert.equal(manifest.engines.vscode, "^1.91.0");
  assert.equal(manifest.main, "./extension.js");
  assert.deepEqual(manifest.activationEvents, ["onLanguage:plcopen-st"]);
  assert.deepEqual(manifest.contributes.languages, [
    {
      id: "plcopen-st",
      aliases: ["PLCopen ST", "Structured Text"],
      extensions: [".st"],
      configuration: "./language-configuration.json",
    },
  ]);
  assert.deepEqual(manifest.capabilities.untrustedWorkspaces, {
    supported: "limited",
    description:
      "不可信工作区只注册 ST 语言配置，不启动 Python Language Server。",
    restrictedConfigurations: ["plcopenSt.pythonPath"],
  });

  const pythonSetting =
    manifest.contributes.configuration.properties["plcopenSt.pythonPath"];
  assert.equal(pythonSetting.default, "python");
  assert.equal(pythonSetting.scope, "machine");
});

test("untrusted workspace registers no process until trust is granted", async () => {
  const harness = makeHarness({ trusted: false });
  const { createExtension } = require("../extension-core.js");
  const extension = createExtension(harness.vscode, harness.languageClient);
  const context = { subscriptions: [] };

  await extension.activate(context);
  assert.equal(harness.clients.length, 0);
  assert.equal(harness.trustListeners.length, 1);
  assert.match(harness.output.join("\n"), /不可信工作区/);

  await harness.trustListeners[0]();
  assert.equal(harness.clients.length, 1);
  assert.equal(harness.clients[0].startCalls, 1);
});

test("trusted workspace starts one stdio client for file and untitled ST", async () => {
  const pythonPath = "C:\\Python\\python.exe";
  const harness = makeHarness({ pythonPath });
  const { createExtension } = require("../extension-core.js");
  const extension = createExtension(harness.vscode, harness.languageClient);

  await extension.activate({ subscriptions: [] });

  assert.equal(harness.clients.length, 1);
  const client = harness.clients[0];
  assert.deepEqual(client.serverOptions, {
    command: pythonPath,
    args: ["-m", "plcopen_lsp"],
    transport: "stdio",
  });
  assert.deepEqual(client.clientOptions.documentSelector, [
    { scheme: "file", language: "plcopen-st" },
    { scheme: "untitled", language: "plcopen-st" },
  ]);
  assert.equal(client.clientOptions.outputChannel !== undefined, true);

  await extension.deactivate();
  assert.equal(client.stopCalls, 1);
});

test("startup failure reports interpreter and install command", async () => {
  const harness = makeHarness({ startError: new Error("module missing") });
  const { createExtension } = require("../extension-core.js");
  const extension = createExtension(harness.vscode, harness.languageClient);

  await extension.activate({ subscriptions: [] });

  assert.equal(harness.errors.length, 1);
  assert.match(harness.errors[0], /plcopenSt\.pythonPath/);
  assert.match(
    harness.errors[0],
    /pip install <path-to-pyplcopen-wheel>/,
  );
  assert.match(harness.output.join("\n"), /module missing/);
});
