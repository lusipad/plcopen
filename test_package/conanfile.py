import os

from conan import ConanFile
from conan.tools.build import can_run
from conan.tools.cmake import CMake, cmake_layout


class PlcopenTestPackageConan(ConanFile):
    test_type = "explicit"
    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeToolchain", "CMakeDeps"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if not can_run(self):
            return
        suffix = ".exe" if self.settings.os == "Windows" else ""
        for exe in ("plcopen_find_package_smoke", "plcopen_st_guide_smoke"):
            self.run(os.path.join(self.cpp.build.bindirs[0], exe + suffix), env="conanrun")
