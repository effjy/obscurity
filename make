gcc -O2 -march=native -Wall -Wextra \
    -fno-strict-aliasing -fno-tree-vectorize \
    -fno-builtin-memcmp -fno-builtin-memset \
    -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
    -fPIE -pie -Wl,-z,relro,-z,now \
    -s -o obscurity obscurity.c -largon2 -lcrypto -lsodium
