#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

std::string getSigningTimestampMs() {
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch());
  return std::to_string(ms.count());
}

struct BIO_Deleter {
  void operator()(BIO *b) const { BIO_free(b); }
};
struct EVP_PKEY_Deleter {
  void operator()(EVP_PKEY *p) const { EVP_PKEY_free(p); }
};
struct EVP_MD_CTX_Deleter {
  void operator()(EVP_MD_CTX *c) const { EVP_MD_CTX_free(c); }
};

std::string generateKalshiSignature(const std::string &message,
                                    const std::string &key_path) {
  // Load private key from file
  std::ifstream key_file(key_path);
  if (!key_file.is_open()) {
    throw std::runtime_error("Could not open private key file: " + key_path);
  }

  // Read the entire file into a string buffer
  std::stringstream buffer;
  buffer << key_file.rdbuf();
  std::string key_content{buffer.str()};

  // Create a memory BIO to wrap the string content safely
  std::unique_ptr<BIO, BIO_Deleter> bio(BIO_new_mem_buf(
      key_content.data(), static_cast<int>(key_content.size())));
  if (!bio) {
    throw std::runtime_error("Failed to create memory BIO");
  }

  // Parse the private key
  std::unique_ptr<EVP_PKEY, EVP_PKEY_Deleter> pkey(
      PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
  if (!pkey) {
    throw std::runtime_error("Failed to read PEM private key");
  }

  std::unique_ptr<EVP_MD_CTX, EVP_MD_CTX_Deleter> ctx(EVP_MD_CTX_new());
  if (!ctx) {
    throw std::runtime_error("Failed to create EVP_MD_CTX");
  }

  EVP_PKEY_CTX *pctx{nullptr}; // Owned by ctx, so no smart pointer needed

  if (EVP_DigestSignInit(ctx.get(), &pctx, EVP_sha256(), nullptr, pkey.get()) <=
      0) {
    throw std::runtime_error("DigestSignInit failed");
  }

  // Set PSS padding and SHA256 for salt/mask
  if (EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) <= 0) {
    throw std::runtime_error("Failed to set RSA padding");
  }
  if (EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, -1) <= 0) {
    throw std::runtime_error("Failed to set RSA PSS salt length");
  }

  std::size_t sig_len{0};
  if (EVP_DigestSign(ctx.get(), nullptr, &sig_len,
                     reinterpret_cast<const uint8_t *>(message.data()),
                     message.size()) <= 0) {
    throw std::runtime_error("Failed to determine signature length");
  }

  std::vector<uint8_t> sig(sig_len);
  if (EVP_DigestSign(ctx.get(), sig.data(), &sig_len,
                     reinterpret_cast<const uint8_t *>(message.data()),
                     message.size()) <= 0) {
    throw std::runtime_error("Signing failed");
  }

  // Base64 encoding
  std::string encoded_sig;
  encoded_sig.resize(4 * ((sig_len + 2) / 3));

  int written{
      EVP_EncodeBlock(reinterpret_cast<unsigned char *>(encoded_sig.data()),
                      sig.data(), sig_len)};
  encoded_sig.resize(written);

  return encoded_sig;
}
