# Building Local Call — Qt 6.11.0 + MSVC 2022

## The problem you likely hit

If you see this error:
```
C:\Qt\6.11.0\mingw_64\include\QtCore\qcompilerdetection.h:
  #error: "Qt requires a C++17 compiler..."
```

It means **two things**:
1. Your Qt 6.11.0 installation only has the **MinGW** kit — the **MSVC 2022** kit is not installed
2. MSVC needs the `/Zc:__cplusplus` flag (now added to CMakeLists.txt automatically)

---

## Step 1 — Install the MSVC 2022 kit + Qt Multimedia (one-time)

1. Open **Qt Maintenance Tool** (Start Menu → Qt → Maintenance Tool)
2. Click **Add or Remove Components**
3. Expand **Qt → Qt 6.11.0** and tick ALL of:
   - ☑ **MSVC 2022 64-bit**
   - ☑ **Qt Multimedia** (under Additional Libraries)
4. Click **Next → Update** and wait

Verify both installed:
```
C:\Qt\6.11.0\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake         ← must exist
C:\Qt\6.11.0\msvc2022_64\lib\cmake\Qt6Multimedia\...cmake      ← must exist
```

---

## Step 2 — Delete the old build folder

The old `build/` folder was generated with the wrong Qt kit.
You MUST delete it before reconfiguring:

```powershell
cd C:\Users\MoeJoe\Downloads\LocalCallPro_CPP
Remove-Item -Recurse -Force build
```

---

## Step 3 — Configure, build, deploy

Open **x64 Native Tools Command Prompt for VS 2022**, then:

```powershell
# Configure  (explicitly point at MSVC kit)
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/msvc2022_64"

# Build
cmake --build build --config Release

# Deploy Qt DLLs
cd build\Release
windeployqt --release LocalCall.exe

# Run
LocalCall.exe
```

---

## Expected cmake output (success)

```
-- Auto-detected Qt at C:/Qt/6.11.0/msvc2022_64
-- Qt6Multimedia found — voice notes and audio calls enabled
=== Local Call build configuration ===
  Platform      : Windows
  Qt Multimedia : TRUE
  OpenCV        : FALSE
==========================================
```

If you see `mingw_64` anywhere in the output, the wrong kit is still selected —
go back to Step 1.
