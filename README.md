# gnuplot

Source code cloned from the original CVS source with additional files and changes to make building with VC2019 easier. 

## Quick Start
Prerequisites:
- Visual Studio 2019
- CMake 3.8.0 or higher (note: downloaded automatically by vcpkg if not found)
- `git.exe` available in your path (note: downloaded automatically by vcpkg if not found)

Clone this repository and the vcpkg repository (https://github.com/Microsoft/vcpkg.git), then run
```
C:\src\vcpkg> .\bootstrap-vcpkg.bat
```
Then, to hook up user-wide integration, run (note: requires admin on first use)
```
C:\src\vcpkg> .\vcpkg integrate install
```
Install pango, libgd and wxwidgets packages with
```
C:\src\vcpkg> .\vcpkg install pango:x86-windows pango:x64-windows
C:\src\vcpkg> .\vcpkg install libgd:x86-windows libgd:x64-windows
C:\src\vcpkg> .\vcpkg install wxwidgets:x86-windows wxwidgets:x64-windows
```

Finally, open the VC2019 solution `config\msvc\gnuplot\gnuplot.sln` and build the solution.

| Branch | Build status |
| ------ | ------------ |
| master | [![Build status](https://ci.appveyor.com/api/projects/status/vnyn8yrfit4wewc0/branch/master?svg=true)](https://ci.appveyor.com/project/AlexanderTaeschner/gnuplot/branch/master) |
