# gnuplot

Source code cloned from the original CVS source with additional files and changes to make building with VC2017 easier. 

## Quick Start
Prerequisites:
- Visual Studio 2017
- CMake 3.8.0 or higher (note: downloaded automatically by vcpkg if not found)
- `git.exe` available in your path

Clone this repository and the vcpkg repository (https://github.com/Microsoft/vcpkg.git), then run
```
C:\src\vcpkg> .\bootstrap-vcpkg.bat
```
Then, to hook up user-wide integration, run (note: requires admin on first use)
```
C:\src\vcpkg> .\vcpkg integrate install
```
Install libgd packages with
```
C:\src\vcpkg> .\vcpkg install libgd
```

Finally, open the VC2017 solution `config\msvc\gnuplot\gnuplot.sln` and build the x86/Debug version.

In the `dev` branch you find further modifications to the source code.