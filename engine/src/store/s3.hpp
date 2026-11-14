// Connecteur S3 : Amazon S3, Backblaze B2, et tout service compatible (Wasabi, MinIO…). Même interface que le
// FTP, le SMB et le WebDAV. Réglages : FtpConfig — user = clé d'accès, password = clé secrète, bucket, region,
// service (« aws », « b2 » ou « other » : host = adresse du service, « https://… » ou « http://hôte:port »),
// root = dossier dans le compartiment. Requêtes signées (AWS Signature Version 4).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "store/ftp.hpp"

namespace pbc::store {

std::unique_ptr<Store> s3(const FtpConfig& cfg, std::string& err);

// Adresse du service : « s3.eu-west-3.amazonaws.com », « s3.us-west-004.backblazeb2.com » ou celle saisie.
std::string s3_endpoint(const FtpConfig& cfg);
// Services proposés par leur nom : aws, b2, wasabi, scaleway, ovh, c2 (Synology C2), r2 (Cloudflare), other.
bool s3_known_service(const std::string& s);

// Signature v4 (exposée pour le test contre l'exemple publié par AWS).
struct S3Sign {
    std::string method, host, path, query;  // path déjà encodé ; query déjà triée et encodée
    std::string amz_date;                   // « 20130524T000000Z »
    std::string region, payload_hash;
    std::vector<std::pair<std::string, std::string>> headers;  // en plus de host, x-amz-date, x-amz-content-sha256
};
std::string s3_authorization(const S3Sign& s, const std::string& access_key, const std::string& secret_key);

}  // namespace pbc::store
