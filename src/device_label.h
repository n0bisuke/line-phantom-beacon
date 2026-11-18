// 通知・テレメトリに付ける端末ラベルの共通定義。
// env の build_flags で PHANTOM_DEVICE_ID が与えられればそれを優先し、
// secrets.h の DEVICE_ID を使う(例: m5stack-atom env → "atom-echo")。
#pragma once

#include "secrets.h"

#if defined(PHANTOM_DEVICE_ID)
#define _PH_STRIFY(x) #x
#define PH_STRIFY(x) _PH_STRIFY(x)
#define DEVICE_LABEL PH_STRIFY(PHANTOM_DEVICE_ID)
#else
#define DEVICE_LABEL DEVICE_ID
#endif
