"use strict";

const fs = require("node:fs");
const path = require("node:path");

const extensionRoot = path.resolve(__dirname, "..");
const lockPath = path.join(extensionRoot, "package-lock.json");
const inventoryPath = path.join(
  extensionRoot,
  "production-dependencies.json",
);
const noticesPath = path.join(extensionRoot, "THIRD_PARTY_NOTICES.md");
const checkOnly = process.argv.includes("--check");

const automaticallyApprovedLicenses = new Set([
  "Apache-2.0",
  "BSD-2-Clause",
  "BSD-3-Clause",
  "MIT",
]);
const licenseExceptions = {
  minimatch: "BlueOak-1.0.0",
  semver: "ISC",
};
const expectedDirectDependencies = {
  "vscode-languageclient": "10.1.0",
};
const expectedDevDependencies = {
  "@vscode/vsce": "3.9.2",
};

function assertExactDependencies(actual, expected, label) {
  if (JSON.stringify(actual) !== JSON.stringify(expected)) {
    throw new Error(
      `${label} 已偏离批准清单：${JSON.stringify(actual)}`,
    );
  }
}

function findLicenseFile(packageDirectory) {
  const candidates = fs
    .readdirSync(packageDirectory)
    .filter((name) => /^(license|copying|notice)(\.|$)/i.test(name))
    .sort();
  if (candidates.length === 0) {
    throw new Error(`${packageDirectory} 缺少许可证文件`);
  }
  return candidates[0];
}

function generate() {
  const lock = JSON.parse(fs.readFileSync(lockPath, "utf8"));
  const root = lock.packages[""];
  assertExactDependencies(
    root.dependencies,
    expectedDirectDependencies,
    "production 直接依赖",
  );
  assertExactDependencies(
    root.devDependencies,
    expectedDevDependencies,
    "dev 直接依赖",
  );

  const dependencies = [];
  for (const [packagePath, locked] of Object.entries(lock.packages)) {
    if (
      !packagePath.startsWith("node_modules/") ||
      locked.dev === true
    ) {
      continue;
    }

    const packageDirectory = path.join(extensionRoot, packagePath);
    const manifest = JSON.parse(
      fs.readFileSync(path.join(packageDirectory, "package.json"), "utf8"),
    );
    if (manifest.version !== locked.version) {
      throw new Error(`${manifest.name} 的 lockfile 与安装版本不一致`);
    }
    const licenseApproved =
      automaticallyApprovedLicenses.has(manifest.license) ||
      licenseExceptions[manifest.name] === manifest.license;
    if (!licenseApproved) {
      throw new Error(
        `${manifest.name}@${manifest.version} 使用未批准许可证 ${manifest.license}`,
      );
    }
    if (!locked.integrity) {
      throw new Error(`${manifest.name}@${manifest.version} 缺少 integrity`);
    }
    const licenseFile = findLicenseFile(packageDirectory);
    dependencies.push({
      name: manifest.name,
      version: manifest.version,
      license: manifest.license,
      integrity: locked.integrity,
      direct: Object.hasOwn(expectedDirectDependencies, manifest.name),
      licenseFile,
      licenseText: fs.readFileSync(
        path.join(packageDirectory, licenseFile),
        "utf8",
      ),
    });
  }
  dependencies.sort((left, right) => left.name.localeCompare(right.name));
  for (const [name, license] of Object.entries(licenseExceptions)) {
    if (
      !dependencies.some(
        (dependency) =>
          dependency.name === name && dependency.license === license,
      )
    ) {
      throw new Error(`批准的许可证例外 ${name} (${license}) 未出现在生产树`);
    }
  }

  const inventory = {
    schemaVersion: 1,
    generatedFrom: "package-lock.json",
    dependencies: dependencies.map(({ licenseText, ...entry }) => entry),
  };
  const inventoryText = `${JSON.stringify(inventory, null, 2)}\n`;

  const noticeLines = [
    "# Third-party notices (draft)",
    "",
    "This draft is generated from the locked production dependency tree.",
    "Development-only packages are not shipped and are intentionally omitted.",
    "",
  ];
  for (const dependency of dependencies) {
    noticeLines.push(
      `## ${dependency.name} ${dependency.version} — ${dependency.license}`,
      "",
      `Integrity: \`${dependency.integrity}\``,
      "",
      ...dependency.licenseText
        .replace(/\r\n/g, "\n")
        .trimEnd()
        .split("\n")
        .map((line) => {
          const withoutTrailingWhitespace = line.trimEnd();
          return withoutTrailingWhitespace
            ? `    ${withoutTrailingWhitespace}`
            : "";
        }),
      "",
    );
  }
  const noticesText = `${noticeLines.join("\n").trimEnd()}\n`;

  return { inventoryText, noticesText };
}

function verifyFile(filePath, expected) {
  if (!fs.existsSync(filePath) || fs.readFileSync(filePath, "utf8") !== expected) {
    throw new Error(
      `${path.basename(filePath)} 已缺失或过期；运行 npm run licenses`,
    );
  }
}

const { inventoryText, noticesText } = generate();
if (checkOnly) {
  verifyFile(inventoryPath, inventoryText);
  verifyFile(noticesPath, noticesText);
} else {
  fs.writeFileSync(inventoryPath, inventoryText);
  fs.writeFileSync(noticesPath, noticesText);
}
