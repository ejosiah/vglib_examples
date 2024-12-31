#ifndef CUCKOO_HASH_COMMON_GLSL
#define CUCKOO_HASH_COMMON_GLSL

#define NULL_LOCATION 0xFFFFFFFFu
#define KEY_EMPTY 0xFFFFFFFFu
#define NOT_FOUND 0xFFFFFFFFu
#define p 334214459u
#define a1 100000u
#define b1 200u
#define a2 300000u
#define b2 489902u
#define a3 800000u
#define b3 10248089u
#define a4 9458373u
#define b4 1234838u
#define KEY 0
#define VALUE 1
#define TABLE_SIZE(prefix) prefix.tableSize

#define hash1(key, prefix) ((a1 ^ key + b1) % p % TABLE_SIZE(prefix))
#define hash2(key, prefix) ((a2 ^ key + b2) % p % TABLE_SIZE(prefix))
#define hash3(key, prefix) ((a3 ^ key + b3) % p % TABLE_SIZE(prefix))
#define hash4(key, prefix) ((a4 ^ key + b4) % p % TABLE_SIZE(prefix))

#endif // CUCKOO_HASH_COMMON_GLSL