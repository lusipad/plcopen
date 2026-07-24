"use strict";

const vscode = require("vscode");
const languageClientModule = require("vscode-languageclient/node");
const { createExtension } = require("./extension-core.js");

let extension;

async function activate(context) {
  extension = createExtension(vscode, languageClientModule);
  await extension.activate(context);
}

async function deactivate() {
  if (extension) {
    await extension.deactivate();
    extension = undefined;
  }
}

module.exports = { activate, deactivate };
