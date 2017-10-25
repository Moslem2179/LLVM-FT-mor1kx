# LLVM-FT-mor1kx
This version of llvm compiler includes several  soft error protection schemes including nZDC, CFCSS, NEMESIS and SWIFT.
This version has backend passes for nZDC and NEMESIS control-flow checking.
Command line:
  llc -enable-ZDC=true -enable-NEMESIS-CF=true  -compat-or1k-delay-filler=true -reserve-18-registers=true  --march=or1k matmul.LL -o MM-ZDC-CF.s
