#!/bin/bash
# Compilation Teensy 4.0 SANS arduino-cli ni paquet PJRC, avec arm-none-eabi-gcc et les sources
# GitHub du core et des bibliothèques. Sert uniquement de vérification locale quand pjrc.com
# n'est pas joignable (environnement de développement) ; la compilation de référence est
# `arduino-cli compile` (voir .github/workflows/build.yml et README.md).
#
# Usage : tools/teensy_local_build.sh <TC>
#   <TC> contient : xpack-arm-none-eabi-gcc-*/  libs/{cores,Audio,SPI,Wire,SD,SdFat,SerialFlash,
#                   OpenAudio_ArduinoLibrary,ILI9341_t3n,EncoderTool,Bounce2}  cmsis/ (CMSIS_5 5.4.0)
# Options reprises de platform.txt / boards.txt de Teensyduino 1.59 (teensy40, 600 MHz, USB Serial).
set -e
shopt -s nullglob
TC=${1:?usage: $0 <toolchain_dir>}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SK=$ROOT/RSCW_Teensy
OUT=$TC/build
mkdir -p "$OUT"
BIN=$(echo "$TC"/xpack-arm-none-eabi-gcc-*/bin)
CC=$BIN/arm-none-eabi-gcc; CXX=$BIN/arm-none-eabi-g++; AR=$BIN/arm-none-eabi-ar
CPU="-mthumb -mcpu=cortex-m7 -mfloat-abi=hard -mfpu=fpv5-d16"
DEFS="-D__IMXRT1062__ -DTEENSYDUINO=159 -DARDUINO=10819 -DARDUINO_TEENSY40 -DF_CPU=600000000 -DUSB_SERIAL -DLAYOUT_US_ENGLISH"
COMMON="-O2 -g -Wall -ffunction-sections -fdata-sections -nostdlib $CPU $DEFS"
CXXF="-std=gnu++17 -fno-exceptions -fpermissive -fno-rtti -fno-threadsafe-statics -felide-constructors -Wno-error=narrowing"
L=$TC/libs
INC="-I$L/cores/teensy4"
for d in Audio Audio/utility SPI Wire Wire/utility SD/src SdFat/src SerialFlash OpenAudio_ArduinoLibrary ILI9341_t3n ILI9341_t3n/src EncoderTool/src Bounce2/src; do
  [ -d "$L/$d" ] && INC="$INC -I$L/$d"
done
INC="$INC -I$SK"

compile_dir() { # $1 = nom de l'archive, $2.. = fichiers
  local name=$1; shift
  local objs=()
  mkdir -p "$OUT/$name"
  for f in "$@"; do
    local o="$OUT/$name/$(basename "$f").o"
    if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
      case "$f" in
        *.c) $CC $COMMON $INC -c "$f" -o "$o" ;;
        *.S) $CC $COMMON -x assembler-with-cpp $INC -c "$f" -o "$o" ;;
        *) $CXX $COMMON $CXXF $INC -c "$f" -o "$o" ;;
      esac || { echo "ECHEC: $f"; exit 1; }
    fi
    objs+=("$o")
  done
  rm -f "$OUT/$name.a"; $AR rcs "$OUT/$name.a" "${objs[@]}"
}

# CMSIS-DSP (la bibliothèque précompilée de Teensyduino n'est pas sur GitHub)
if [ ! -f "$OUT/libarm_math.a" ]; then
  mkdir -p "$OUT/cmsis"
  for f in "$TC"/cmsis/CMSIS/DSP/Source/*/*.c; do
    case "$f" in *Functions.c|*Tables.c) continue ;; esac   # fichiers « tout-en-un »
    $CC -O2 $CPU -DARM_MATH_CM7 -D__FPU_PRESENT=1 -ffunction-sections -fdata-sections \
      -I"$TC"/cmsis/CMSIS/DSP/Include -I"$TC"/cmsis/CMSIS/Core/Include -c "$f" -o "$OUT/cmsis/$(basename "$f").o"
  done
  $AR rcs "$OUT/libarm_math.a" "$OUT"/cmsis/*.o
fi

compile_dir core $L/cores/teensy4/*.c $L/cores/teensy4/*.cpp $L/cores/teensy4/*.S
compile_dir libs $L/Audio/*.cpp $L/Audio/*.c $L/Audio/utility/*.c $L/Audio/utility/*.cpp \
  $L/SPI/*.cpp $L/Wire/*.cpp $L/Wire/utility/*.c* $L/SD/src/*.cpp \
  $(find $L/SdFat/src -name '*.cpp') $L/SerialFlash/*.cpp \
  $L/OpenAudio_ArduinoLibrary/*.cpp $L/OpenAudio_ArduinoLibrary/*.c \
  $L/ILI9341_t3n/src/*.cpp $L/ILI9341_t3n/src/*.c \
  $(find $L/EncoderTool/src -name '*.cpp') $L/Bounce2/src/*.cpp
cp "$SK/RSCW_Teensy.ino" "$OUT/RSCW_Teensy.ino.cpp"
compile_dir sketch "$OUT/RSCW_Teensy.ino.cpp" "$SK"/*.cpp

$CXX -O2 -Wl,--gc-sections,--relax $CPU -T$L/cores/teensy4/imxrt1062.ld \
  -o "$OUT/RSCW_Teensy.elf" "$OUT"/sketch/*.o \
  -Wl,--whole-archive "$OUT/core.a" -Wl,--no-whole-archive "$OUT/libs.a" \
  -L"$OUT" -larm_math -lm -lstdc++
$BIN/arm-none-eabi-size -A "$OUT/RSCW_Teensy.elf" | grep -E "^\.(text|data|bss|dmabuffers|bss.dma|text.itcm|text.progmem)|Total"
echo "OK: $OUT/RSCW_Teensy.elf"
