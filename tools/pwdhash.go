package main

import (
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"os"
)

func main() {
	if len(os.Args) <= 1 {
		fmt.Printf("usage: pwdhash PASSWORD\n")
		return
	}

	fmt.Printf("password = \"%v\"\n", os.Args[1])

	var salt [32]byte
	if n, err := rand.Read(salt[:]); err != nil || n != 32 {
		fmt.Printf("Failed to generate salt: %v\n", err)
	}

	secret := sha256.Sum256([]byte(os.Args[1]))
	for i := 0; i < 32; i += 1 {
		secret[i] ^= salt[i]
	}

	pwdhash := sha256.Sum256(secret[:])

	fmt.Printf("pwdhash = %v\n", hex.EncodeToString(pwdhash[:]))
	fmt.Printf("salt    = %v\n", hex.EncodeToString(salt[:]))
}
