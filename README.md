# line-phantom-beacon

PlatformIO の電子工作プロジェクト。ボード: M5Stack AtomS3(`board = m5stack-atoms3`, espressif32)、フレームワーク: `arduino`

## 前提
- `shared-dev-environments` を `../../shared-dev-environments` に配置(または `SHARED_DEV_ENV_DIR` を指定)。
- 共通イメージ `shared-pio:latest` をビルド済みにする(`docker image ls shared-pio` で確認):
  `cd shared-dev-environments && npm run build:pio`

## 使い方(ビルド)
```bash
# shared-dev-environments から移動する場合
cd ../8_embedded-workspaces/line-phantom-beacon
docker compose run --rm pio          # コンテナ内 /workspace で bash が開く
pio run                              # ビルド。初回はツールチェーンをダウンロードするので時間がかかる
```
ビルド成果物は `.pio/build/m5stack-atoms3/`(ホストからも見える。Git管理外)。

## 書き込み・シリアルモニタ
macOS の Docker はコンテナに USB を渡せないため、**書き込みはホスト側**で行う。

**A. ホストの PlatformIO を使う(全ボード共通)**
```bash
# ホストのMacで(PlatformIO Core か VS Code の PlatformIO 拡張が必要)
pio run -t upload
pio device monitor
```

**B. ESP32 をコンテナから書き込む(RFC2217 経由。未検証)**
```bash
# ホストのMacで(pip install esptool が必要)。ポート名は ls /dev/cu.* で確認
esp_rfc2217_server -v -p 4000 /dev/cu.usbserial-XXXX
```
`platformio.ini` の `upload_port` / `monitor_port` のコメントを外し、コンテナ内で `pio run -t upload` / `pio device monitor`。

## Volume
- 共通: `pio-core`(ツールチェーン等), `pio-claude-config`
- 専用: ソース(バインドマウント `.:/workspace`、`.pio/` を含む)
