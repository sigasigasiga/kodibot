{
  description = "kodibot — Telegram echo bot built on TDLib";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];

      forAllSystems = f:
        nixpkgs.lib.genAttrs systems (system: f {
          inherit system;
          pkgs = import nixpkgs { inherit system; };
        });

      mkShell = { pkgs, system }:
        let
          llvmPkgs = pkgs.llvmPackages_22;
          stdenv = llvmPkgs.stdenv;
        in
        (pkgs.mkShell.override { inherit stdenv; }) {
          name = "kodibot-dev";

          nativeBuildInputs = [
            pkgs.cmake
            pkgs.ninja
            pkgs.pkg-config
            pkgs.gperf
            llvmPkgs.clang-tools
            llvmPkgs.lld
            llvmPkgs.lldb
          ];

          buildInputs = [
            pkgs.openssl
            pkgs.zlib
            pkgs.spdlog
            pkgs.httplib
            pkgs.boost
          ];

          shellHook = ''
            export CC=${stdenv.cc}/bin/clang
            export CXX=${stdenv.cc}/bin/clang++
            export CMAKE_GENERATOR=Ninja
          '';
        };
    in
    {
      devShells = forAllSystems ({ pkgs, system }: {
        default = mkShell { inherit pkgs system; };
      });

      formatter = forAllSystems ({ pkgs, ... }: pkgs.nixfmt-rfc-style);
    };
}
