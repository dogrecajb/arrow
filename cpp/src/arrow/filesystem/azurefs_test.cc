// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <algorithm>  // Missing include in boost/process

// This boost/asio/io_context.hpp include is needless for no MinGW
// build.
//
// This is for including boost/asio/detail/socket_types.hpp before any
// "#include <windows.h>". boost/asio/detail/socket_types.hpp doesn't
// work if windows.h is already included. boost/process.h ->
// boost/process/args.hpp -> boost/process/detail/basic_cmd.hpp
// includes windows.h. boost/process/args.hpp is included before
// boost/process/async.h that includes
// boost/asio/detail/socket_types.hpp implicitly is included.
#include <boost/asio/io_context.hpp>
// We need BOOST_USE_WINDOWS_H definition with MinGW when we use
// boost/process.hpp. See BOOST_USE_WINDOWS_H=1 in
// cpp/cmake_modules/ThirdpartyToolchain.cmake for details.
#include <boost/process.hpp>

#include "arrow/filesystem/azurefs.h"
#include "arrow/util/io_util.h"

#include <gmock/gmock-matchers.h>
#include <gmock/gmock-more-matchers.h>
#include <gtest/gtest.h>

#include <string>

#include "arrow/testing/gtest_util.h"
#include "arrow/testing/util.h"

#include <azure/identity/client_secret_credential.hpp>
#include <azure/identity/default_azure_credential.hpp>
#include <azure/identity/managed_identity_credential.hpp>
#include <azure/storage/blobs.hpp>
#include <azure/storage/common/storage_credential.hpp>

namespace arrow {
using internal::TemporaryDir;
namespace fs {
namespace {
namespace bp = boost::process;

using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::NotNull;

class AzuriteEnv : public ::testing::Environment {
 public:
  AzuriteEnv() {
    account_name_ = "devstoreaccount1";
    account_key_ =
        "Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/"
        "KBHBeksoGMGw==";
    auto exe_path = bp::search_path("azurite");
    if (exe_path.empty()) {
      auto error = std::string("Could not find Azurite emulator.");
      status_ = Status::Invalid(error);
      return;
    }
    auto temp_dir_ = *TemporaryDir::Make("azurefs-test-");
    server_process_ = bp::child(boost::this_process::environment(), exe_path, "--silent",
                                "--location", temp_dir_->path().ToString(), "--debug",
                                temp_dir_->path().ToString() + "/debug.log");
    if (!(server_process_.valid() && server_process_.running())) {
      auto error = "Could not start Azurite emulator.";
      server_process_.terminate();
      server_process_.wait();
      status_ = Status::Invalid(error);
      return;
    }
    status_ = Status::OK();
  }

  ~AzuriteEnv() override {
    server_process_.terminate();
    server_process_.wait();
  }

  const std::string& account_name() const { return account_name_; }
  const std::string& account_key() const { return account_key_; }
  const Status status() const { return status_; }

 private:
  std::string account_name_;
  std::string account_key_;
  bp::child server_process_;
  Status status_;
  std::unique_ptr<TemporaryDir> temp_dir_;
};

auto* azurite_env = ::testing::AddGlobalTestEnvironment(new AzuriteEnv);

AzuriteEnv* GetAzuriteEnv() {
  return ::arrow::internal::checked_cast<AzuriteEnv*>(azurite_env);
}

// Placeholder tests
// TODO: GH-18014 Remove once a proper test is added
TEST(AzureFileSystem, UploadThenDownload) {
  const std::string container_name = "sample-container";
  const std::string blob_name = "sample-blob.txt";
  const std::string blob_content = "Hello Azure!";

  const std::string& account_name = GetAzuriteEnv()->account_name();
  const std::string& account_key = GetAzuriteEnv()->account_key();

  auto credential = std::make_shared<Azure::Storage::StorageSharedKeyCredential>(
      account_name, account_key);

  auto service_client = Azure::Storage::Blobs::BlobServiceClient(
      std::string("http://127.0.0.1:10000/") + account_name, credential);
  auto container_client = service_client.GetBlobContainerClient(container_name);
  container_client.CreateIfNotExists();
  auto blob_client = container_client.GetBlockBlobClient(blob_name);

  std::vector<uint8_t> buffer(blob_content.begin(), blob_content.end());
  blob_client.UploadFrom(buffer.data(), buffer.size());

  std::vector<uint8_t> downloaded_content(blob_content.size());
  blob_client.DownloadTo(downloaded_content.data(), downloaded_content.size());

  EXPECT_EQ(std::string(downloaded_content.begin(), downloaded_content.end()),
            blob_content);
}

TEST(AzureFileSystem, InitializeCredentials) {
  auto default_credential = std::make_shared<Azure::Identity::DefaultAzureCredential>();
  auto managed_identity_credential =
      std::make_shared<Azure::Identity::ManagedIdentityCredential>();
  auto service_principal_credential =
      std::make_shared<Azure::Identity::ClientSecretCredential>("tenant_id", "client_id",
                                                                "client_secret");
}

TEST(AzureFileSystem, OptionsCompare) {
  AzureOptions options;
  EXPECT_TRUE(options.Equals(options));
}

}  // namespace
}  // namespace fs
}  // namespace arrow
