# SHMの導入
[[English](../md_manual_tutorials_introduction_en.html) | 日本語]

# 開発環境構築

このライブラリはPOSIXに準拠したOS（主にLinux）を対象としている。
以下ではUbuntuで行った開発環境の構築手順を示す。
例えば、CentOSなどではaptをyumに置き換えるなど適宜変更が必要である。

1. C++コンパイラとビルドツールのインストール
   ```
   sudo apt update && sudo apt install build-essential
   ```

2. CMakeとGitのインストール
   ```
   sudo apt install cmake git
   ```

3. PythonおよびBoost.Pythonのインストール(オプション)
   ```
   sudo apt install python3-dev libboost-python-dev
   ```

4. ワークスペースの作成
   ```
   cd {ワークスペースを作成したい場所}
   mkdir -p cmake_ws/src
   ```

5. SHMのクローン
   ```
   cd cmake_ws/src/
   git clone https://github.com/ir-utsunomiya/shared-memory-based-handy-communication-manager.git
   ```

6. ワークスペース直下のCMakeLists.txtの作成
   ```
   cd {ワークスペースを作成した場所}/cmake_ws/
   gedit CMakeLists.txt
   ```
   開いたエディタに以下のテキストをコピーして保存
   ```
   cmake_minimum_required(VERSION 3.16)
   cmake_policy(SET CMP0060 NEW)

   project(MobileRobot_cmake)
   set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_BINARY_DIR}/cmake)
   set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR})
   set(CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR})
   SET(CMAKE_INSTALL_RPATH ${CMAKE_BINARY_DIR}/lib)

   add_subdirectory(src)
   ```

7. srcディレクトリのCMakeLists.txtの作成
   ```
   cd {ワークスペースを作成した場所}/cmake_ws/src/
   gedit CMakeLists.txt
   ```
   開いたエディタに以下のテキストをコピーして保存
   ```
   cmake_minimum_required(VERSION 3.16)

   add_subdirectory(shared-memory-based-handy-communication-manager)
   ```

# ビルド

1. buildディレクトリの作成
   ```
   cd {ワークスペースを作成した場所}/cmake_ws
   mkdir build
   ```

2. cmakeの実行
   ```
   cd build
   cmake ..
   ```

3. makeの実行
   ```
   make
   ```
