{ pkgs ? import <nixpkgs> {} }:

with pkgs;

mkShell {
    buildInputs =
        let duckdf = pkgs.rPackages.buildRPackage {
                name = "duckdf";
                src = pkgs.fetchFromGitHub {
                    owner = "phillc73";
                    repo = "duckdf";
                    rev = "2fd57fe";
                    sha256 = "sha256-qpVm3qQxRE9cfnFEsCSt44YUXTlmxzm0J3KKqncyzuA=";
                };
                propagatedBuildInputs = with pkgs.rPackages; [ duckdb DBI stringi ];
            };
            util = [ R gdb python39 drawio entr ];
            tex = [
                texlive.combined.scheme-full
                python39Packages.pygments
                python39Packages.python-lsp-server
            ];
            r = with pkgs.rPackages; [
                Cairo
                ggplot2
                ggpubr
                ggpattern
                sqldf
                dplyr
                tidyr
                purrr
                directlabels
                ggrepel
                lubridate
                slider
                stringr
                RColorBrewer
                shades
                memoise
                forcats
                duckdf
            ];
            qc = [
                sbcl
                gcc13
                clang_16
                liburing
                jemalloc
                bzip2
                snappy
                zstd
                lz4
                tbb_2021_8
                # btrblocks
                boost
            ];
        in util ++ tex ++ r ++ qc;

    nativeBuildInputs = [ clang-tools_16 ];

}
