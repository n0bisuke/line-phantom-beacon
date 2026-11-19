# ESPectre 3.2.0 / 本プロジェクト 機能調査: 実装済み・不可能・実装可能

(2026-11-19, Fable 5.2 調査。ソース根拠: `.pio/libdeps/<env>/ESPectre/src/` の実コードで確認したものに限る)

## 1. 実装済み(現コードで動作しているもの)

| 機能 | 場所 | 実機検証 |
|---|---|---|
| CSI 動き検知 → 3段階疑似イベント(10/20/30s)→ LINE push | `lib/phantom_events`, `src/main.cpp` | ✅ 全4ボード |
| トラフィック生成モード選択: WIFI_RAW(既定)/ DNS / PING | `src/motion_sensor.cpp` の `PHANTOM_TRAFFIC_DNS/PING` | ✅ C3(occ1.0)/C6(DNS, occ0.9) |
| multi-AP Wi-Fi 切替(切断時のみ最強APへ) | `src/motion_sensor.cpp` | ✅ C3(2AP切替確認) |
| sensing ready gating(キャリブ中・ready往復時の誤push抑制) | `src/main.cpp` feedEvents | ✅ |
| ボード別表示: LCD(AtomS3)/ RGB LED(Echo)/ 単色LED点滅(C3,C6) | `src/display.cpp` | ✅ |
| LINE push ラベル(env別 `PHANTOM_DEVICE_ID`)+ 再送抑止 | `src/line_push_client.cpp` | ✅ |
| LINE 無効運用(未設定でビルド/動作を壊さない) | 同上 | ✅ |
| Grafana Cloud Loki テレメトリ(イベント+presence+heartbeat) | `src/telemetry_client.cpp` | ✅ C3 |
| TLS 証明書検証(LINE=GlobalSign R46 / Grafana=DigiCert Root G2) | `src/line_root_ca.h`, `telemetry_root_ca.h` | ✅ |
| ユニットテスト(lib/phantom_events, line_beacon_frame 17/17) | `test/` | ✅ |

## 2. 実装**不可能**なもの(物理・ライブラリの硬い制約)

| 機能 | 制約の内容 |
|---|---|
| **ESP32-C6 で WIFI_RAW** | ESPectre 3.2.0 が `runtime_traffic_generator_mode_supported` で明示拒否(`runtime_config_utils.cpp` → `runtime_config_utils.cpp:103-108`)。代替: DNS(実装済み) |
| **検知領域の空間限定**(「部屋Aだけ」) | CSI は電波経路全体の乱れを見る仕様で、部屋単位の境界設定は原理的に無い。キャリブレーションは「変動基準」を作るだけで空間フィルタではない |
| **動きの検知位置・部屋の区別**(どの部屋にいるか) | CSI 発生源の空間情報が観測値に残らない。隣室と自分の部屋の動きを区別できない |
| **呼吸・寝返りなどの微小動作** | Lightweight 検知器は歩行級の動きの設計。微小動作を安定検知する保証は Lightweight/High-Accuracy どちらにも無い |
| **Echo スピーカーの音(現状)** | Class-D+DAC 駆動(GPIO25)の構成。PWM→DAC 化したが実機で音を確認するまでに至っていない(要デバッグ、コード的には不可能ではない) |
| **5GHz CSI(手持ちのチップ)** | CSI は 2.4GHz 802.11n のみ。`wifi_band_policy=BAND_5G` の設定は存在するが**デュアルバンドチップ(C5 等)が前提**で手持ちの S3/C3/C6 は全て 2.4GHz 専用(`runtime_config.h` のコメント) |

## 3. 実装**可能**(ソース確認済み・未実装のもの)

| 機能 | 根拠(ESPectre API) | 効果/用途 |
|---|---|---|
| **検知アルゴリズム切替: LIGHTWEIGHT ↔ HIGH_ACCURACY** | `runtime_config.h` → `detection_algorithm`(`runtime_sensing_schema.h:40-45`) | **High-Accuracy は起動キャリブレーション不要**・学習済みモデル(`core/ml_weights.h`)・設定したしきい値をそのまま使う。キャリブレーション時の「部屋を空ける/静止」の手間が消える。※スイッチ実装を一度作って調査用に戻済み(`motion_sensor.cpp` の差分、復活は1箇所) |
| **しきい値の手動設定** | `config.threshold`(High-Accuracy では設定値を保持し続ける) | 設置後の感度微調整。「過検知で寝返りが反応する」→ しきい値を上げるなど |
| **デバウンス調整: motion_on_hits / motion_off_hits** | `apply_runtime_control_update`(`runtime_config_utils.cpp:120-123`) | 検知の応答速度と誤発の安定性のトレードオフを数回のヒット数で調整 |
| **検知窓の長さ: window_size_ms(1000..2000)** | `runtime_config.h` | 短く=瞬発力、長く=安定寄り |
| **CSI 目標レート: csi_target_pps** | 同上(既定100) | より高頻度のCSI → 高速検知(メモリ/CPU増) |
| **CSI サンプル間隔: half_slot / distinct** | `csi_sample_spacing` | temporal sampling の疎密 |
| **キャプチャプロファイル: AUTO / LLTF / HT_VHT** | `csi_capture_policy` | 捕らえるプリアンブル種別を絞る(WIFI_RAW は AUTO/LLTF 専用: `runtime_config_utils.cpp:110-113`) |
| **トラフィック: DNS_TCP / EXTERNAL_HOST(自前サーバのUDP)** | `TrafficGeneratorMode` (`runtime_sensing_schema.h:47-`) | DNS クエリのTCP化/自分のサーバから CSI の供給(自社LAN運用向け) |
| **検知器選択の有効化 + NVS 永続化** | `runtime_detector_selection_enabled=true`、`device_config_store.h` / `runtime_traffic_mode_store.cpp` | 起動時に NVS から検知器/トラフィックモードを復元(設定変更が持続する) |
| **検知窓セグメンテーション: segmentation_threshold / window_size_ms** | `RuntimeConfigError::SEGMENTATION_*` | 動きパターンの分割検出(詳細は公式ドキュメント参照) |

## 4. 不確実(ソースに存在するが Arduino 版での実動作が未確認のもの)

- MQTT/HTTP の runtime フロントエンド(`frontend_command_engine` / `runtime_frontend_controller` が lib 内に存在)。公式 README で「Arduino edition は sensing-only(no MQTT/HTTP/provisioning)」と明言があるため、これらは ESP-IDF エディション専用の可能性が高い
- EXTERNAL_HOST モードの正確な挙動(UDP の送受信どちらを観測するか)はパケット実装(`traffic_generator_service.cpp`)を精読する必要あり

## 5. ディスカッション用メモ(部屋A/B・キャリブレーション絡み)

- 検知領域は「ルーターと ESP32 を結ぶ直線」ではなく **CSI を観測している電波経路全体**(隣室・玄関・条件次第で屋外の窓際も)。キャリブレーションは空間フィルタではなく「変動基準」を作る
- **High-Accuracy モードはキャリブレーション不要** ⇒ 部屋Bに退避させてから設置する手間が無い。ただし「どの部屋かの区別」は結局できない(2部屋の動きが混在した CSI になる)
- 先回りとして本番で有意味な設定: **High-Accuracy + 手動しきい値** を secrets/env 一発で切り替えられるようにする実装が、キャリブレーション周りの体験を最も改善する(1番上の可能テーブルに含めた)
