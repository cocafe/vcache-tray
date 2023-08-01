# VCACHE-TRAY

A tray utility to control dynamic CCD preference of heterogeneous AMD 3D V-Cache CPUs like 7950x3D and 7900x3D. Some privileged games ~~(aka. GenSh1t )~~ and programs are refused to set affinity, this utility may help to overcome this case to schedule these bad behaved programs to preferred CCD without locking preferred CCD in BIOS.

![](./asset/preview.png)


### Requirements

- Recommended installing **latest** BIOS and AMD chipset driver.
  - At least BIOS and drivers which support 7000x3D CPUs.
- Option `CPPC Dynamic Preferred Cores` in BIOS menu `AMD CBS/SMU` must be set to `AUTO` or `Driver` to allow OS to control via `AMD 3D V-Cache Performance Optimizer` device
  - Otherwise this device will not work and appear in Device Manager.
- Recommend turning off `Game Mode` and `xbox Game Bar` in Windows, since you are gonna use this utility to control CCD preferences and profiles manually.



### Note

- ⚠ On ASUS ProArt-x670E, BIOS 1202, if `Max Clock Limit` is enabled in `PBO` menu, both BIOS and driver `CPPC prefer CCD` function will not work properly, dunno why.
- ⚠ Recommended switch `prefer CCD` or setup profile before starting new program, it looks like some activated threads will stick on original CCD until they are terminated.
- ⚠ `Prefer CCD` may not affect programs or threads that have been set affinity by themselves, manually or other process management software like `ProcessLasso`.
- ⚠ `Reset Service Forcefully` option may cause buggy AMD 3dvcache or system stuck, use with caution!
  - `Restart Service on Apply` is NOT recommended, just wait for 3dvcache polling the registry changes, although this may take a while sometimes.


### How to use

Run it and switch modes via tray icon.

To start with Windows, create a task in `Task Scheduler`.

Pass `-h` to check more options, `--alloc_console` to show debug logs.



### How does it work

While I was investigating 3D V-Cache driver and planning to take over control of device, but I found that AMD put a registry interface `HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\amd3dvcache\Preferences` to allow change things on the fly. It will take a **while**, like, up to several minutes to take effect without restarting the 3dvcache service. That's it, this program is just a simple frontend of these registry interfaces.


### Tweaks

This utility also provides some simple tweaks to ZEN4.

Tweaks can be disabled completely via config json or argument `--no_tweaks`.

Tweaks will be applied again after S3/S4 resume.

 - `Package C6`: recommended disabling it to reduce freeze/shutter in gaming
 - `Core C1E`: recommended disabling it to reduce freeze/shutter in gaming
   - disabled by default, enable it will cause CPU package power display differently in HWinfo
 - `Core C6`: recommended disabling it to reduce freeze/shutter in gaming
   - disabled by default if `Meidum Load Boostit` enabled in BIOS
   - boost freq may be limited if multiple cores are activated at the same time without `Core C6` enabled, this may behave differently on various BIOS/CPU, please check on your platform.
 - `No C-state Timers`: some c-state features and timers will be disabled, with this tweak, tiny freezes/shutters in some games seems are eliminated
   - for more details, please check source code for what this tweak does
 - `CPB`: something like TurboBoost on blue brand
 - `Perf Bias`: some perf bias tweaks collected from Web and BIOS, may help in some specified workloads
   - `Default`: will not touch anything
   - These tweaks write some undocumented MSRs, not sure what they control, some of those MSRs control something critical like `op-cache`, which can have a performance impact in some workloads 


### Build

Build with msys2 and cmake.

