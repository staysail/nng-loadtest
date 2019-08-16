#!/bin/bash


# create the key
openssl genrsa -out key.key

# create the cert request
openssl req -new -key key.key -out cert.csr -sha256

# create the self signed cert
openssl x509 -req -in cert.csr -days 36500 -out cert.crt -signkey key.key -sha256

exit 0
// Relevant metadata:
//
// Certificate:
//    Data:
//        Version: 1 (0x0)
//        Serial Number: 17127835813110005400 (0xedb24becc3a2be98)
//    Signature Algorithm: sha256WithRSAEncryption
//        Issuer: C=US, ST=CA, L=San Diego, O=nanomsg.org, CN=localhost
//        Validity
//            Not Before: Jan 11 22:34:35 2018 GMT
//            Not After : Dec 18 22:34:35 2117 GMT
//        Subject: C=US, ST=CA, L=San Diego, O=nanomsg.org, CN=localhost
//        Subject Public Key Info:
//            Public Key Algorithm: rsaEncryption
//                Public-Key: (2048 bit)
//
