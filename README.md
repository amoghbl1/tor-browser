## ORFOX BUILD STEPS:

1) Move .mozconfig-android to .mozconfig OR run: 
```
export MOZCONFIG="tor-browser/.mozconfig-android"
```
2) Checks if the all requirements for the build are fine with:
```    
./mach configure
```
3) Builds the repo with:
```
./mach build
```
4) Creates the apk in tor-browser/MOZ_OBJDIR/dist/fennec-38.0.en-US.android-arm.apk
```
./mach package
```
### Note: this does not ship the addons, that is managed in a different repo: https://github.com/amoghbl1/orfox-addons.
### Steps to include these addons can be figured out looking at the jenkins script at https://github.com/amoghbl1/Orfox/blob/master/jenkins-build
