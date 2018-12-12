/*
 * Copyright 2015-2018 the original author or authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <memory>

#include "kotlin_generator.h"
#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/compiler/plugin.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <iostream>

static string KotlinPackageToDir(const string& package_name) {
  string package_dir = package_name;
  for (size_t i = 0; i < package_dir.size(); ++i) {
    if (package_dir[i] == '.') {
      package_dir[i] = '/';
    }
  }
  if (!package_dir.empty()) package_dir += "/";
  return package_dir;
}

class KotlinRSocketRpcGenerator : public google::protobuf::compiler::CodeGenerator {
 public:
  KotlinRSocketRpcGenerator() {}
  virtual ~KotlinRSocketRpcGenerator() {}

  virtual bool Generate(const google::protobuf::FileDescriptor* file,
                        const string& parameter,
                        google::protobuf::compiler::GeneratorContext* context,
                        string* error) const {
    return reactor(file, parameter, context, error);
  }

  virtual bool reactor(const google::protobuf::FileDescriptor* file,
                          const string& parameter,
                          google::protobuf::compiler::GeneratorContext* context,
                          string* error) const {
    std::vector<std::pair<string, string> > options;
    google::protobuf::compiler::ParseGeneratorParameter(parameter, &options);

    kotlin_rsocket_rpc_generator::ProtoFlavor flavor =
     kotlin_rsocket_rpc_generator::ProtoFlavor::NORMAL;

    bool disable_version = false;
    for (size_t i = 0; i < options.size(); i++) {
        if (options[i].first == "lite") {
            flavor = kotlin_rsocket_rpc_generator::ProtoFlavor::LITE;
        } else if (options[i].first == "noversion") {
            disable_version = true;
        }
    }

    string package_name = kotlin_rsocket_rpc_generator::ServiceKotlinPackage(file);
    string package_filename = KotlinPackageToDir(package_name);
    for (int i = 0; i < file->service_count(); ++i) {
        const google::protobuf::ServiceDescriptor* service = file->service(i);

        string interface_filename = package_filename + service->name() + ".kt";
        std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream> interface_file(context->Open(interface_filename));
        kotlin_rsocket_rpc_generator::GenerateInterface(service, interface_file.get(), flavor, disable_version);

        string client_filename = package_filename + kotlin_rsocket_rpc_generator::ClientClassName(service) + ".kt";
        std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream> client_file(context->Open(client_filename));
        kotlin_rsocket_rpc_generator::GenerateClient(service, client_file.get(), flavor, disable_version);

        string server_filename = package_filename + kotlin_rsocket_rpc_generator::ServerClassName(service) + ".kt";
        std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream> server_file(context->Open(server_filename));
        kotlin_rsocket_rpc_generator::GenerateServer(service, server_file.get(), flavor, disable_version);
    }
    return true;
  }
};

int main(int argc, char* argv[]) {
  KotlinRSocketRpcGenerator generator;
  return google::protobuf::compiler::PluginMain(argc, argv, &generator);
}
