# shared-dev-environments/pio Dockerfile へのフィードバック

2026-11-12の line-phantom-beacon 開発で分かった、大元のテンプレへの反映案。
(Dockerfile 自体は既に build-essential / esptool / gh が入っており、大半は要件を満たしている)

## 1. `pio remote` の推奨フローを ENVIRONMENT.md に書いてほしい

書き込み・モニタの現実解は `pio remote`。現状の ENVIRONMENT.md には
RFC2217のみ載っているが、実際に通ったのは:

- ホスト: `pio account login` → `pio remote agent start`
- コンテナ: 環境変数 `PLATFORMIO_AUTH_TOKEN` でトークン認証
  (PlatformIO 6.2.0 は `pio account login --token` が無いが、環境変数は
  `account/client.py` の `fetch_authentication_token()` で優先読み取りされる)
  → `pio remote run -t upload` / `pio remote device monitor`

この方式の利点は、**エージェント側に espressif32 ツールチェーン(約3GB)が不要**
なこと。リモートアップロードではコンテナ側のビルド成果物がクラウド経由で
agentに渡されるため、ホストには pio(CLI)と contrib-pioremote だけで良い。
macOS + OrbStack 環境で特に効く(OS側に3GBを置かずに済む)。

文面の例:
```bash
# コンテナ内
export PLATFORMIO_AUTH_TOKEN=<トークン>   # .env で渡すのが楽
pio remote run -t upload
pio remote device monitor
```

## 2. イメージに contrib-pioremote を積むと初回が速い

`pio remote xxx` の初回実行時に `platformio/contrib-pioremote` と
Twisted 系の依存が pip で入る(実測で初回のみ数十秒〜数分)。
Dockerfile の末尾に以下を足しておくとオフライン感覚で使える:

```dockerfile
RUN pio pkg install -g -t platformio/contrib-pioremote
```

## 3. ネイティブテスト用の記述は現状どおりで良い

`build-essential` 追加後の `[env:native]` テストは快調(16/16が数十秒)。
1点だけ: PlatformIO のテストスイートは **`test/test_<名前>/test_*.cpp`** の
形でないと収集されない(ディレクトリが `test_*` から始まらないと無視される)。
ENVIRONMENT.md の「ネイティブテストについて」に1行書いておくと事故らない:

```
- テストスイートは test/test_<名前>/test_*.cpp の形で置く
  (test/<名前>/ や test 直下はスイートとみなされず "Nothing to build" になる)
```

## 4. (軽微) OrbStack での USB を使う場合の注意書き

- `orb usb attach` すると macOS 側からデバイスが消える(detach+抜き差しで戻る)。
- ボード定義に CDC_ON_BOOT の無いボード(AtomS3 等)では、ホスト側のシリアルが
  空っぽに見えるが、それはファームに `-DARDUINO_USB_CDC_ON_BOOT=1` が無い
  ことが原因。ボード側の定義の問題なのでイメージ側の問題ではない。

## 5. (軽微) .env.sample に pio remote 用の定数を足す提案

```
# pio remote (書き込み/モニタをコンテナから実行する場合)
PLATFORMIO_AUTH_TOKEN=
```
