# pico-auth-lab

Raspberry Pi Picoで、組み込みセキュリティを次の順番で学ぶための実験用リポジトリです。

1. [Secure Boot](lessons/01-secure-boot/README.md)
2. [Code Signing](lessons/02-code-signing/README.md)
3. [Secure Update](lessons/03-secure-update/README.md)
4. [TLS](lessons/04-tls/README.md)
5. [Secure Storage](lessons/05-secure-storage/README.md)
6. [Access Control](lessons/06-access-control/README.md)

各章は前の章で作った仕組みを前提にします。まず各章のREADMEで「守るもの」「攻撃」「完了条件」を確認し、`TODO`を一つずつ実装してください。完成コードを先に置かず、実験と失敗から理解できる構成にしています。

## 重要な前提

この教材は、Pico上でセキュリティの仕組みを自作して理解するためのものです。RP2040自体には、一般的なマイコンのようにROMが署名済みファームウェアだけを起動するハードウェア強制型Secure Bootはありません。そのため、本教材の第1〜3章はブートローダーによるソフトウェア検証を題材にします。学習用実装をそのまま製品のセキュリティ境界には使えません。

また、暗号アルゴリズムの自作は理解のために限定します。第4章以降の実用的な通信では、十分にレビューされた暗号ライブラリを使う方針です。

## 必要なもの

- Raspberry Pi Pico（RP2040）
- Pico SDK
- CMakeとCコンパイラ
- USBケーブル

## 始め方

```sh
cd pico
./run.sh build
```

生成物は `pico/build/pico_auth_lab.uf2` です。書き込みまで行う場合は `./run.sh` を実行します。

## 学習ルール

- 章を飛ばさない
- 秘密値、署名、ハッシュを同じものとして扱わない
- 成功ケースだけでなく、1 byte破壊した入力が拒否されることを確認する
- 固定長バッファを使い、`malloc`は使わない
- 各章の完了条件を満たしてから次へ進む
