/*
 * Copyright (c) 2020 Belledonne Communications SARL.
 *
 * This file is part of bctoolbox.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <mbedtls/error.h>
#include <mbedtls/version.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/gcm.h>


#include "bctoolbox/crypto.hh"
#include "bctoolbox/exception.hh"
#include "bctoolbox/logging.h"


namespace bctoolbox {

/*****************************************************************************/
/***                      Random Number Generation                         ***/
/*****************************************************************************/

/**
 * @brief Wrapper around mbedtls implementation
 **/
struct RNG::Impl {
	mbedtls_entropy_context entropy; /**< entropy context - store it to be able to free it */
	mbedtls_ctr_drbg_context ctr_drbg; /**< rng context */

	/**
	 * Implementation constructor
	 * Initialise the entropy and RNG context
	 */
	Impl() {
		mbedtls_entropy_init(&entropy);
		mbedtls_ctr_drbg_init(&ctr_drbg);
		if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0) != 0) {
			throw BCTBX_EXCEPTION << "RNG failure at creation: entropy source failure";
		}
	}
	~Impl() {
		mbedtls_ctr_drbg_free(&ctr_drbg);
		mbedtls_entropy_free(&entropy);
	}
};

/**
 * Constructor
 * Just instanciate an implementation
 */
RNG::RNG()
:pImpl(std::unique_ptr<RNG::Impl>(new RNG::Impl())) {};

/**
 * Destructor
 * Implementation is stored in a unique_ptr, nothing to do
 **/
RNG::~RNG()=default;

// Instanciate the class RNG context
std::unique_ptr<RNG::Impl> RNG::pImplClass = std::unique_ptr<RNG::Impl>(new RNG::Impl());


void RNG::randomize(uint8_t *buffer, size_t size) {
	int ret = mbedtls_ctr_drbg_random(&(pImpl->ctr_drbg), buffer, size);
	if ( ret != 0) {
		throw BCTBX_EXCEPTION << ((ret == MBEDTLS_ERR_CTR_DRBG_REQUEST_TOO_BIG)?"RNG failure: Request too big":"RNG failure: entropy source failure");
	}
}

std::vector<uint8_t> RNG::randomize(const size_t size) {
	std::vector<uint8_t> buffer(size);
	int ret = mbedtls_ctr_drbg_random(&(pImpl->ctr_drbg), buffer.data(), size);
	if ( ret != 0) {
		throw BCTBX_EXCEPTION << ((ret == MBEDTLS_ERR_CTR_DRBG_REQUEST_TOO_BIG)?"RNG failure: Request too big":"RNG failure: entropy source failure");
	}
	return buffer;
}

uint32_t RNG::randomize() {
	uint8_t buffer[4];
	randomize(buffer, 4);
	return (buffer[0]<<24) | (buffer[1]<<16) | (buffer[2]<<8) | buffer[3];
}

/*
 * class randomize functions
 * These use the class RNG context
 */
void RNG::c_randomize(uint8_t *buffer, size_t size) {
	int ret = mbedtls_ctr_drbg_random(&(pImplClass->ctr_drbg), buffer, size);
	if ( ret != 0) {
		throw BCTBX_EXCEPTION << ((ret == MBEDTLS_ERR_CTR_DRBG_REQUEST_TOO_BIG)?"RNG failure: Request too big":"RNG failure: entropy source failure");
	}
}

uint32_t RNG::c_randomize() {
	uint8_t buffer[4];
	c_randomize(buffer, 4);
	return (buffer[0]<<24) | (buffer[1]<<16) | (buffer[2]<<8) | buffer[3];
}

/*****************************************************************************/
/***                      Hash related function                            ***/
/*****************************************************************************/
/* HMAC templates */
/* HMAC must use a specialized template */
template <typename hashAlgo>
std::array<uint8_t, hashAlgo::ssize()> HMAC(const std::vector<uint8_t> &key, const std::vector<uint8_t> &input) {
	/* if this template is instanciated the static_assert will fail but will give us an error message */
	static_assert(sizeof(hashAlgo) != sizeof(hashAlgo), "You must specialize HMAC function template");
}

/* HMAC specialized template for SHA256 */
template <> std::array<uint8_t, SHA256::ssize()> HMAC<SHA256>(const std::vector<uint8_t> &key, const std::vector<uint8_t> &input) {
	std::array<uint8_t, SHA256::ssize()> hmacOutput;
	mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key.data(), key.size(), input.data(), input.size(), hmacOutput.data());
	return  hmacOutput;
}

/* HMAC specialized template for SHA384 */
template <> std::array<uint8_t, SHA384::ssize()> HMAC<SHA384>(const std::vector<uint8_t> &key, const std::vector<uint8_t> &input) {
	std::array<uint8_t, SHA384::ssize()> hmacOutput;
	mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA384), key.data(), key.size(), input.data(), input.size(), hmacOutput.data());
	return  hmacOutput;
}

/* HMAC specialized template for SHA512 */
template <> std::array<uint8_t, SHA512::ssize()> HMAC<SHA512>(const std::vector<uint8_t> &key, const std::vector<uint8_t> &input) {
	std::array<uint8_t, SHA512::ssize()> hmacOutput;
	mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA512), key.data(), key.size(), input.data(), input.size(), hmacOutput.data());
	return  hmacOutput;
}

/* HKDF templates */
/* HKDF must use a specialized template */
template <typename hashAlgo>
std::vector<uint8_t> HKDF(const std::vector<uint8_t> &salt, const std::vector<uint8_t> &ikm, const std::vector<uint8_t> &info, size_t okmSize) {
	/* if this template is instanciated the static_assert will fail but will give us an error message */
	static_assert(sizeof(hashAlgo) != sizeof(hashAlgo), "You must specialize HKDF function template");

	return std::vector<uint8_t>(0);
}
template <typename hashAlgo>
std::vector<uint8_t> HKDF(const std::vector<uint8_t> &salt, const std::vector<uint8_t> &ikm, const std::string &info, size_t okmSize) {
	/* if this template is instanciated the static_assert will fail but will give us an error message */
	static_assert(sizeof(hashAlgo) != sizeof(hashAlgo), "You must specialize HKDF function template");

	return std::vector<uint8_t>(0);
}

/* HKDF specialized template for SHA256 */
template <> std::vector<uint8_t> HKDF<SHA256>(const std::vector<uint8_t> &salt, const std::vector<uint8_t> &ikm, const std::vector<uint8_t> &info, size_t outputSize) {
	std::vector<uint8_t> okm(outputSize);
	if (mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), salt.data(), salt.size(), ikm.data(), ikm.size(), info.data(), info.size(), okm.data(), outputSize) != 0) {
		throw BCTBX_EXCEPTION<<"HKDF-SHA256 error";
	}
	return okm;
};
template <> std::vector<uint8_t> HKDF<SHA256>(const std::vector<uint8_t> &salt, const std::vector<uint8_t> &ikm, const std::string &info, size_t outputSize) {
	std::vector<uint8_t> okm(outputSize);
	if (mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), salt.data(), salt.size(), ikm.data(), ikm.size(), reinterpret_cast<const unsigned char*>(info.data()), info.size(), okm.data(), outputSize) != 0) {
		throw BCTBX_EXCEPTION<<"HKDF-SHA256 error";
	}
	return okm;
};

/* HKDF specialized template for SHA512 */
template <> std::vector<uint8_t> HKDF<SHA512>(const std::vector<uint8_t> &salt, const std::vector<uint8_t> &ikm, const std::vector<uint8_t> &info, size_t outputSize) {
	std::vector<uint8_t> okm(outputSize);
	if (mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA512), salt.data(), salt.size(), ikm.data(), ikm.size(), info.data(), info.size(), okm.data(), outputSize) != 0) {
		throw BCTBX_EXCEPTION<<"HKDF-SHA512 error";
	}
	return okm;
};
template <> std::vector<uint8_t> HKDF<SHA512>(const std::vector<uint8_t> &salt, const std::vector<uint8_t> &ikm, const std::string &info, size_t outputSize) {
	std::vector<uint8_t> okm(outputSize);
	if (mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA512), salt.data(), salt.size(), ikm.data(), ikm.size(), reinterpret_cast<const unsigned char*>(info.data()), info.size(), okm.data(), outputSize) != 0) {
		throw BCTBX_EXCEPTION<<"HKDF-SHA512 error";
	}
	return okm;
};

/*****************************************************************************/
/***                      Authenticated Encryption                         ***/
/*****************************************************************************/
/* AEAD template must be specialized */
template <typename AEADAlgo>
std::vector<uint8_t> AEAD_encrypt(const std::array<uint8_t, AEADAlgo::keySize()> &key, const std::vector<uint8_t> IV, const std::vector<uint8_t> &plain, const std::vector<uint8_t> &AD,
		std::array<uint8_t, AEADAlgo::tagSize()> &tag) {
	/* if this template is instanciated the static_assert will fail but will give us an error message with faulty type */
	static_assert(sizeof(AEADAlgo) != sizeof(AEADAlgo), "You must specialize AEAD_encrypt function template");
	return std::vector<uint8_t>(0);
}

template <typename AEADAlgo>
bool AEAD_decrypt(const std::array<uint8_t, AEADAlgo::keySize()> &key, const std::vector<uint8_t> &IV, const std::vector<uint8_t> &cipher, const std::vector<uint8_t> &AD,
		const std::array<uint8_t, AEADAlgo::tagSize()> &tag, std::vector<uint8_t> &plain) {
	/* if this template is instanciated the static_assert will fail but will give us an error message with faulty type */
	static_assert(sizeof(AEADAlgo) != sizeof(AEADAlgo), "You must specialize AEAD_encrypt function template");
	return false;
}

/* declare AEAD template specialisations : AES256-GCM with 128 bits auth tag*/
template <> std::vector<uint8_t> AEAD_encrypt<AES256GCM128>(const std::array<uint8_t, AES256GCM128::keySize()> &key, const std::vector<uint8_t> IV, const std::vector<uint8_t> &plain, const std::vector<uint8_t> &AD,
		std::array<uint8_t, AES256GCM128::tagSize()> &tag) {
	mbedtls_gcm_context gcmContext;
	mbedtls_gcm_init(&gcmContext);

	auto ret = mbedtls_gcm_setkey(&gcmContext, MBEDTLS_CIPHER_ID_AES, key.data(), key.size()*8); // key size in bits
	if (ret != 0) {
		mbedtls_gcm_free(&gcmContext);
		throw BCTBX_EXCEPTION<<"Unable to set key in AES_GCM context : return value "<<ret;
	}

	std::vector<uint8_t> cipher(plain.size()); // cipher size is the same than plain
	ret = mbedtls_gcm_crypt_and_tag(&gcmContext, MBEDTLS_GCM_ENCRYPT, plain.size(), IV.data(), IV.size(), AD.data(), AD.size(), plain.data(), cipher.data(), tag.size(), tag.data());
	mbedtls_gcm_free(&gcmContext);

	if (ret != 0) {
		throw BCTBX_EXCEPTION<<"Error during AES_GCM encryption : return value "<<ret;
	}
	return cipher;
}

template <> bool AEAD_decrypt<AES256GCM128>(const std::array<uint8_t, AES256GCM128::keySize()> &key, const std::vector<uint8_t> &IV, const std::vector<uint8_t> &cipher, const std::vector<uint8_t> &AD,
		const std::array<uint8_t, AES256GCM128::tagSize()> &tag, std::vector<uint8_t> &plain) {
	mbedtls_gcm_context gcmContext;
	mbedtls_gcm_init(&gcmContext);

	auto ret = mbedtls_gcm_setkey(&gcmContext, MBEDTLS_CIPHER_ID_AES, key.data(), key.size()*8); // key size in bits
	if (ret != 0) {
		mbedtls_gcm_free(&gcmContext);
		throw BCTBX_EXCEPTION<<"Unable to set key in AES_GCM context : return value "<<ret;
	}

	plain.resize(cipher.size()); // plain is the same size than cipher
	ret = mbedtls_gcm_auth_decrypt(&gcmContext, cipher.size(), IV.data(), IV.size(), AD.data(), AD.size(), tag.data(), tag.size(), cipher.data(), plain.data());
	mbedtls_gcm_free(&gcmContext);

	if (ret == 0) {
		return true;
	}
	if (ret == MBEDTLS_ERR_GCM_AUTH_FAILED) {
		return false;
	}

	throw BCTBX_EXCEPTION<<"Error during AES_GCM decryption : return value "<<ret;
}


} // namespace bctoolbox