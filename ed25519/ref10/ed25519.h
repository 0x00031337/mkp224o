int ed25519_ref10_seckey(unsigned char *sk);
int ed25519_ref10_seckey_expand(unsigned char *sk,const unsigned char *seed);
int ed25519_ref10_pubkey(unsigned char *pk,const unsigned char *sk);
int ed25519_ref10_keygen(unsigned char *pk,unsigned char *sk);
