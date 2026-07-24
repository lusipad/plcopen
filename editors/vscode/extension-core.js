"use strict";

function createExtension(vscode, languageClientModule) {
  const { LanguageClient, TransportKind } = languageClientModule;
  let client;
  let outputChannel;
  let startPromise;
  let disposed = false;

  function formatFailure(pythonPath, error) {
    const detail = error instanceof Error ? error.message : String(error);
    return [
      `PLCopen ST Language Server 无法启动：${detail}`,
      `解释器：${pythonPath}`,
      `请确认“plcopenSt.pythonPath”指向已安装 pyplcopen 的 Python，`,
      `并执行：${pythonPath} -m pip install pyplcopen`,
    ].join(" ");
  }

  async function startClient() {
    if (disposed || client) {
      return;
    }
    if (startPromise) {
      return startPromise;
    }

    startPromise = (async () => {
      const pythonPath = vscode.workspace
        .getConfiguration("plcopenSt")
        .get("pythonPath", "python");
      let failureReported = false;
      const reportFailure = async (error) => {
        if (failureReported) {
          return;
        }
        failureReported = true;
        const message = formatFailure(pythonPath, error);
        outputChannel.appendLine(message);
        await vscode.window.showErrorMessage(message);
      };
      const candidate = new LanguageClient(
        "plcopen-st",
        "PLCopen ST Language Server",
        {
          command: pythonPath,
          args: ["-m", "plcopen_lsp"],
          transport: TransportKind.stdio,
        },
        {
          documentSelector: [
            { scheme: "file", language: "plcopen-st" },
            { scheme: "untitled", language: "plcopen-st" },
          ],
          outputChannel,
          initializationFailedHandler(error) {
            void reportFailure(error);
            return false;
          },
        },
      );
      client = candidate;
      try {
        await candidate.start();
        outputChannel.appendLine(
          `已启动：${pythonPath} -m plcopen_lsp`,
        );
      } catch (error) {
        if (client === candidate) {
          client = undefined;
        }
        await reportFailure(error);
      }
    })();

    try {
      await startPromise;
    } finally {
      startPromise = undefined;
    }
  }

  async function activate(context) {
    outputChannel = vscode.window.createOutputChannel("PLCopen ST");
    context.subscriptions.push(outputChannel);
    context.subscriptions.push(
      vscode.workspace.onDidGrantWorkspaceTrust(() => startClient()),
    );

    if (vscode.workspace.isTrusted) {
      await startClient();
    } else {
      outputChannel.appendLine(
        "不可信工作区：已注册 PLCopen ST 语言配置，但不会启动 Python 进程。",
      );
    }
  }

  async function deactivate() {
    disposed = true;
    if (startPromise) {
      await startPromise;
    }
    const activeClient = client;
    client = undefined;
    if (activeClient) {
      await activeClient.stop();
    }
  }

  return { activate, deactivate };
}

module.exports = { createExtension };
