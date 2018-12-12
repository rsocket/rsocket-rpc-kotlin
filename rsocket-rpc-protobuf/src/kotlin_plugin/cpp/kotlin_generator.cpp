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

#include "kotlin_generator.h"
#include "rsocket/options.pb.h"

#include <algorithm>
#include <iostream>
#include <iterator>
#include <map>
#include <vector>
#include <google/protobuf/compiler/java/java_names.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>

// Stringify helpers used solely to cast rsocket_rpc_version
#ifndef STR
#define STR(s) #s
#endif

#ifndef XSTR
#define XSTR(s) STR(s)
#endif

#ifndef FALLTHROUGH_INTENDED
#define FALLTHROUGH_INTENDED
#endif

namespace kotlin_rsocket_rpc_generator {

using google::protobuf::FileDescriptor;
using google::protobuf::ServiceDescriptor;
using google::protobuf::MethodDescriptor;
using google::protobuf::Descriptor;
using google::protobuf::io::Printer;
using google::protobuf::SourceLocation;
using io::rsocket::rpc::RSocketMethodOptions;

// Adjust a method name prefix identifier to follow the KotlinBean spec:
//   - decapitalize the first letter
//   - remove embedded underscores & capitalize the following letter
static string MixedLower(const string& word) {
  string w;
  w += tolower(word[0]);
  bool after_underscore = false;
  for (size_t i = 1; i < word.length(); ++i) {
    if (word[i] == '_') {
      after_underscore = true;
    } else {
      w += after_underscore ? toupper(word[i]) : word[i];
      after_underscore = false;
    }
  }
  return w;
}

// Converts to the identifier to the ALL_UPPER_CASE format.
//   - An underscore is inserted where a lower case letter is followed by an
//     upper case letter.
//   - All letters are converted to upper case
static string ToAllUpperCase(const string& word) {
  string w;
  for (size_t i = 0; i < word.length(); ++i) {
    w += toupper(word[i]);
    if ((i < word.length() - 1) && islower(word[i]) && isupper(word[i + 1])) {
      w += '_';
    }
  }
  return w;
}

static inline string LowerMethodName(const MethodDescriptor* method) {
  return MixedLower(method->name());
}

static inline string MethodFieldName(const MethodDescriptor* method) {
  return "METHOD_" + ToAllUpperCase(method->name());
}

static inline string MessageFullKotlinName(const Descriptor* desc) {
  return google::protobuf::compiler::java::ClassName(desc);
}

static inline string ServiceFieldName(const ServiceDescriptor* service) { return "SERVICE"; }

template <typename ITR>
static void SplitStringToIteratorUsing(const string& full,
                                       const char* delim,
                                       ITR& result) {
  // Optimize the common case where delim is a single character.
  if (delim[0] != '\0' && delim[1] == '\0') {
    char c = delim[0];
    const char* p = full.data();
    const char* end = p + full.size();
    while (p != end) {
      if (*p == c) {
        ++p;
      } else {
        const char* start = p;
        while (++p != end && *p != c);
        *result++ = string(start, p - start);
      }
    }
    return;
  }

  string::size_type begin_index, end_index;
  begin_index = full.find_first_not_of(delim);
  while (begin_index != string::npos) {
    end_index = full.find_first_of(delim, begin_index);
    if (end_index == string::npos) {
      *result++ = full.substr(begin_index);
      return;
    }
    *result++ = full.substr(begin_index, (end_index - begin_index));
    begin_index = full.find_first_not_of(delim, end_index);
  }
}

static void SplitStringUsing(const string& full,
                             const char* delim,
                             std::vector<string>* result) {
  std::back_insert_iterator< std::vector<string> > it(*result);
  SplitStringToIteratorUsing(full, delim, it);
}

static std::vector<string> Split(const string& full, const char* delim) {
  std::vector<string> result;
  SplitStringUsing(full, delim, &result);
  return result;
}

static string EscapeKotlindoc(const string& input) {
  string result;
  result.reserve(input.size() * 2);

  char prev = '*';

  for (string::size_type i = 0; i < input.size(); i++) {
    char c = input[i];
    switch (c) {
      case '*':
        // Avoid "/*".
        if (prev == '/') {
          result.append("&#42;");
        } else {
          result.push_back(c);
        }
        break;
      case '/':
        // Avoid "*/".
        if (prev == '*') {
          result.append("&#47;");
        } else {
          result.push_back(c);
        }
        break;
      case '@':
        // '@' starts javadoc tags including the @deprecated tag, which will
        // cause a compile-time error if inserted before a declaration that
        // does not have a corresponding @Deprecated annotation.
        result.append("&#64;");
        break;
      case '<':
        // Avoid interpretation as HTML.
        result.append("&lt;");
        break;
      case '>':
        // Avoid interpretation as HTML.
        result.append("&gt;");
        break;
      case '&':
        // Avoid interpretation as HTML.
        result.append("&amp;");
        break;
      case '\\':
        // Kotlin interprets Unicode escape sequences anywhere!
        result.append("&#92;");
        break;
      default:
        result.push_back(c);
        break;
    }

    prev = c;
  }

  return result;
}

template <typename DescriptorType>
static string GetCommentsForDescriptor(const DescriptorType* descriptor) {
  SourceLocation location;
  if (descriptor->GetSourceLocation(&location)) {
    return location.leading_comments.empty() ?
      location.trailing_comments : location.leading_comments;
  }
  return string();
}

static std::vector<string> GetDocLines(const string& comments) {
  if (!comments.empty()) {
    // Ideally we should parse the comment text as Markdown and
    // write it back as HTML, but this requires a Markdown parser.  For now
    // we just use <pre> to get fixed-width text formatting.

    // If the comment itself contains block comment start or end markers,
    // HTML-escape them so that they don't accidentally close the doc comment.
    string escapedComments = EscapeKotlindoc(comments);

    std::vector<string> lines = Split(escapedComments, "\n");
    while (!lines.empty() && lines.back().empty()) {
      lines.pop_back();
    }
    return lines;
  }
  return std::vector<string>();
}

template <typename DescriptorType>
static std::vector<string> GetDocLinesForDescriptor(const DescriptorType* descriptor) {
  return GetDocLines(GetCommentsForDescriptor(descriptor));
}

static void WriteDocCommentBody(Printer* printer,
                                    const std::vector<string>& lines,
                                    bool surroundWithPreTag) {
  if (!lines.empty()) {
    if (surroundWithPreTag) {
      printer->Print(" * <pre>\n");
    }

    for (size_t i = 0; i < lines.size(); i++) {
      // Most lines should start with a space.  Watch out for lines that start
      // with a /, since putting that right after the leading asterisk will
      // close the comment.
      if (!lines[i].empty() && lines[i][0] == '/') {
        printer->Print(" * $line$\n", "line", lines[i]);
      } else {
        printer->Print(" *$line$\n", "line", lines[i]);
      }
    }

    if (surroundWithPreTag) {
      printer->Print(" * </pre>\n");
    }
  }
}

static void WriteDocComment(Printer* printer, const string& comments) {
  printer->Print("/**\n");
  std::vector<string> lines = GetDocLines(comments);
  WriteDocCommentBody(printer, lines, false);
  printer->Print(" */\n");
}

static void WriteServiceDocComment(Printer* printer,
                                       const ServiceDescriptor* service) {
  // Deviating from protobuf to avoid extraneous docs
  // (see https://github.com/google/protobuf/issues/1406);
  printer->Print("/**\n");
  std::vector<string> lines = GetDocLinesForDescriptor(service);
  WriteDocCommentBody(printer, lines, true);
  printer->Print(" */\n");
}

void WriteMethodDocComment(Printer* printer,
                           const MethodDescriptor* method) {
  // Deviating from protobuf to avoid extraneous docs
  // (see https://github.com/google/protobuf/issues/1406);
  printer->Print("/**\n");
  std::vector<string> lines = GetDocLinesForDescriptor(method);
  WriteDocCommentBody(printer, lines, true);
  printer->Print(" */\n");
}

static void PrintInterface(const ServiceDescriptor* service,
                           std::map<string, string>* vars,
                           Printer* p,
                           ProtoFlavor flavor,
                           bool disable_version) {
  (*vars)["service_name"] = service->name();
  (*vars)["service_field_name"] = ServiceFieldName(service);
  (*vars)["file_name"] = service->file()->name();
  (*vars)["rsocket_rpc_version"] = "";
  #ifdef rsocket_rpc_version
  if (!disable_version) {
    (*vars)["rsocket_rpc_version"] = " (version " XSTR(rsocket_rpc_version) ")";
  }
  #endif
  WriteServiceDocComment(p, service);
  p->Print(
      *vars,
      "@$Generated$(\n"
      "    value = [\"by RSocket RPC proto compiler$rsocket_rpc_version$\"],\n"
      "    comments = \"Source: $file_name$\")\n"
      "interface $service_name$ {\n\n");
  p->Indent();
  p->Print("companion object {\n");
  p->Indent();
  // Service IDs
  p->Print(*vars, "const val $service_field_name$ = \"$Package$$service_name$\"\n");

  for (int i = 0; i < service->method_count(); ++i) {
    const MethodDescriptor* method = service->method(i);
    (*vars)["method_field_name"] = MethodFieldName(method);
    (*vars)["method_name"] = method->name();
    p->Print(*vars, "const val $method_field_name$ = \"$method_name$\"\n");
  }
  p->Outdent();
  p->Print("}\n");

  // RPC methods
  for (int i = 0; i < service->method_count(); ++i) {
    const MethodDescriptor* method = service->method(i);
    const RSocketMethodOptions options = method->options().GetExtension(io::rsocket::rpc::options);
    (*vars)["input_type"] = MessageFullKotlinName(method->input_type());
    (*vars)["output_type"] = MessageFullKotlinName(method->output_type());
    (*vars)["lower_method_name"] = LowerMethodName(method);
    bool client_streaming = method->client_streaming();
    bool server_streaming = method->server_streaming();

    // Method signature
    p->Print("\n");
    WriteMethodDocComment(p, method);

    p->Print(*vars, "fun $lower_method_name$");

    if (client_streaming) {
          // Bidirectional streaming or client streaming
          p->Print(*vars, "(message: $Publisher$<$input_type$>, metadata: $ByteBuf$)\n");
        } else {
          // Server streaming or simple RPC
          p->Print(*vars, "(message: $input_type$, metadata: $ByteBuf$)\n");
        }

    if (server_streaming) {
      p->Print(*vars, ":$Flowable$<$output_type$> = $Flowable$.error(UnsupportedOperationException(\"not implemented\"))\n");
    } else if (client_streaming) {
      p->Print(*vars, ":$Single$<$output_type$> = $Single$.error(UnsupportedOperationException(\"not implemented\"))\n");
    } else {
      if (options.fire_and_forget()) {
        p->Print(*vars, ":$Completable$ = $Completable$.error(UnsupportedOperationException(\"not implemented\"))\n");
      } else {
        p->Print(*vars, ":$Single$<$output_type$> = $Single$.error(UnsupportedOperationException(\"not implemented\"))\n");
      }
    }
  }

  p->Outdent();
  p->Print("}\n");
}

static void PrintClient(const ServiceDescriptor* service,
                        std::map<string, string>* vars,
                        Printer* p,
                        ProtoFlavor flavor,
                        bool disable_version) {
  (*vars)["service_name"] = service->name();
  (*vars)["service_field_name"] = ServiceFieldName(service);

  (*vars)["file_name"] = service->file()->name();
  (*vars)["client_class_name"] = ClientClassName(service);
  (*vars)["rsocket_rpc_version"] = "";
  (*vars)["version"] = "";
  #ifdef rsocket_rpc_version
  if (!disable_version) {
    (*vars)["rsocket_rpc_version"] = " (version " XSTR(RSOCKET_RPC_VERSION) ")";
    (*vars)["version"] = XSTR(rsocket_rpc_version);
  }
  #endif
  p->Print(
      *vars,
      "@$Generated$(\n"
      "    value = [\"by RSocket RPC proto compiler$rsocket_rpc_version$\"],\n"
      "    comments = \"Source: $file_name$\")\n"
      "@$RSocketRpcGenerated$(\n"
      "    type = $RSocketRpcResourceType$.CLIENT,\n"
      "    idlClass = $service_name$::class)\n"
      "class $client_class_name$(\n");
      p->Indent();
      p->Print(
            *vars,
      "private val rSocket: $RSocket$, \n"
      "tracer: $Optional$.Optional<$Tracer$> = $Optional$.None\n"
      "): $service_name$ {\n"
      );
  p->Indent();

  // Tracing
  for (int i = 0; i < service->method_count(); ++i) {
      const MethodDescriptor* method = service->method(i);
      const RSocketMethodOptions options = method->options().GetExtension(io::rsocket::rpc::options);
      (*vars)["output_type"] = MessageFullKotlinName(method->output_type());
      (*vars)["lower_method_name"] = LowerMethodName(method);
      bool client_streaming = method->client_streaming();
      bool server_streaming = method->server_streaming();

      if (server_streaming) {
        p->Print(
            *vars,
            "private val $lower_method_name$Trace: ($Map$<String, String>) -> io.reactivex.FlowableTransformer<$output_type$, $output_type$>\n");
      } else if (client_streaming) {
        p->Print(
            *vars,
            "private val $lower_method_name$Trace: ($Map$<String, String>) -> io.reactivex.FlowableTransformer<$output_type$, $output_type$>\n");
      } else {
        const Descriptor* output_type = method->output_type();
        if (options.fire_and_forget()) {
          p->Print(
              *vars,
              "private val $lower_method_name$Trace: ($Map$<String, String>) -> io.reactivex.FlowableTransformer<$output_type$, $output_type$>\n");
        } else {
          p->Print(
              *vars,
            "private val $lower_method_name$Trace: ($Map$<String, String>) -> io.reactivex.FlowableTransformer<$output_type$, $output_type$>\n");
        }
      }
    }

  p->Print("\ninit {\n");

  p->Indent();
  p->Print("when(tracer) {\n");
  p->Indent();
  p->Print(*vars,
  "is $Optional$.None -> {\n" );
  p->Indent();

  // Tracing metrics
  for (int i = 0; i < service->method_count(); ++i) {
    const MethodDescriptor* method = service->method(i);
    (*vars)["lower_method_name"] = LowerMethodName(method);

    p->Print(
        *vars,
        "this.$lower_method_name$Trace = $RSocketRpcTracing$.trace()\n");
  }

  p->Outdent();
  p->Print("}\n");

  // RSocket and Tracing
  p->Print(
      *vars,
      "is $Optional$.Some -> {\n");
  p->Indent();

  // Tracing metrics
  for (int i = 0; i < service->method_count(); ++i) {
    const MethodDescriptor* method = service->method(i);
    (*vars)["lower_method_name"] = LowerMethodName(method);
    (*vars)["method_field_name"] = MethodFieldName(method);

    p->Print(
        *vars,
        "this.$lower_method_name$Trace = $RSocketRpcTracing$.trace(\n");
    p->Indent();
    p->Print(
        *vars,
        "tracer.value,\n"
        "$service_name$.$method_field_name$,\n"
        "$Tag$.of(\"rsocket.service\", $service_name$.$service_field_name$),\n"
        "$Tag$.of(\"rsocket.type\", \"client\"),\n"
        "$Tag$.of(\"rsocket.version\", \"$version$\")\n");
    p->Outdent();
    p->Print(")\n");
  }

  p->Outdent();
  p->Print("}\n");
  p->Outdent();
  p->Print("}\n");
  p->Outdent();
  p->Print("}\n\n");

  // RPC methods
  for (int i = 0; i < service->method_count(); ++i) {
    const MethodDescriptor* method = service->method(i);
    const RSocketMethodOptions options = method->options().GetExtension(io::rsocket::rpc::options);
    (*vars)["input_type"] = MessageFullKotlinName(method->input_type());
    (*vars)["output_type"] = MessageFullKotlinName(method->output_type());
    (*vars)["lower_method_name"] = LowerMethodName(method);
    (*vars)["method_field_name"] = MethodFieldName(method);
    bool client_streaming = method->client_streaming();
    bool server_streaming = method->server_streaming();

    // Method name, no metadata
    if (server_streaming) {
      p->Print(
          *vars,
          "@$RSocketRpcGeneratedMethod$(returnTypeClass = $output_type$::class)\n"
          "fun $lower_method_name$");
    } else if (client_streaming) {
      p->Print(
          *vars,
          "@$RSocketRpcGeneratedMethod$(returnTypeClass = $output_type$::class)\n"
          "fun $lower_method_name$");
    } else {
      const Descriptor* output_type = method->output_type();
      if (options.fire_and_forget()) {
        p->Print(
            *vars,
            "@$RSocketRpcGeneratedMethod$(returnTypeClass = $output_type$::class)\n"
            "fun $lower_method_name$");
      } else {
        p->Print(
            *vars,
            "@$RSocketRpcGeneratedMethod$(returnTypeClass = $output_type$::class)\n"
            "fun $lower_method_name$");
      }
    }

    // Method arg, no metadata
    if (client_streaming) {
          p->Print(
              *vars,
              "(message: $Publisher$<$input_type$>)");
    } else {
          // Server streaming or simple RPC
          p->Print(
              *vars,
              "(message: $input_type$)");
    }

// Method return types and bodies, no metadata
  if (server_streaming) {
      p->Print(
          *vars,
          ": $Flowable$<$output_type$> {\n");
      p->Indent();
      p->Print(
          *vars,
          "return $lower_method_name$(message, $Unpooled$.EMPTY_BUFFER)\n");
      p->Outdent();
      p->Print("}\n\n");
    } else if (client_streaming) {
      p->Print(
          *vars,
          ": $Single$<$output_type$> {\n");
      p->Indent();
      p->Print(
           *vars,
           "return $lower_method_name$(message, $Unpooled$.EMPTY_BUFFER)\n");
      p->Outdent();
      p->Print("}\n\n");
    } else {
      const Descriptor* output_type = method->output_type();
      if (options.fire_and_forget()) {
        p->Print(
            *vars,
            ": $Completable$ {\n");
        p->Indent();
        p->Print(
             *vars,
             "return $lower_method_name$(message, $Unpooled$.EMPTY_BUFFER)\n");
        p->Outdent();
        p->Print("}\n\n");
      } else {
        p->Print(
            *vars,
            ": $Single$<$output_type$> {\n");
        p->Indent();
        p->Print(
            *vars,
            "return $lower_method_name$(message, $Unpooled$.EMPTY_BUFFER)\n");
        p->Outdent();
        p->Print("}\n\n");
      }
    }

    // Method name, with metadata
    if (server_streaming) {
      p->Print(
          *vars,
          "@$RSocketRpcGeneratedMethod$(returnTypeClass = $output_type$::class)\n"
          "override fun $lower_method_name$");
    } else if (client_streaming) {
      p->Print(
          *vars,
          "@$RSocketRpcGeneratedMethod$(returnTypeClass = $output_type$::class)\n"
          "override fun $lower_method_name$");
    } else {
      if (options.fire_and_forget()) {
        p->Print(
            *vars,
            "@$RSocketRpcGeneratedMethod$(returnTypeClass = $output_type$::class)\n"
            "override fun $lower_method_name$");
      } else {
        p->Print(
            *vars,
            "@$RSocketRpcGeneratedMethod$(returnTypeClass = $output_type$::class)\n"
            "override fun $lower_method_name$");
      }
    }

    //method args, with metadata
    if (client_streaming) {
          // Bidirectional streaming or client streaming
          p->Print(
              *vars,
              "(message: $Publisher$<$input_type$>, metadata: $ByteBuf$)\n"
              );
          p->Indent();
    } else {
          // Server streaming or simple RPC
          p->Print(
              *vars,
              "(message: $input_type$, metadata: $ByteBuf$)\n"
              );
          p->Indent();
    }

// Method return value, with metadata
    if (server_streaming) {
      p->Print(
          *vars,
          ": $Flowable$<$output_type$> {\n"
          );
    } else if (client_streaming) {
      p->Print(
          *vars,
          ": $Single$<$output_type$> {\n"
           );
    } else {
      if (options.fire_and_forget()) {
        p->Print(
            *vars,
            ": $Completable$ {\n"
            );
      } else {
        p->Print(
            *vars,
            ": $Single$<$output_type$> {\n"
             );
      }
    }

    // method bodies, with metadata
    if (client_streaming) {
      // Bidirectional streaming or client streaming
      p->Print(
          *vars,
          "val map:$Map$<String, String> = $HashMap$()\n"
          );
      p->Indent();
      p->Print(
          *vars,
          "return rSocket.requestChannel($Flowable$.$from$(message).map(\n");
      p->Indent();
      p->Print(
          *vars,
          "object : $Function$<$MessageLite$, $Payload$> {\n");
      p->Indent();
      p->Print(
          *vars,
          "private val once = $AtomicBoolean$(false)\n\n"
          "override fun apply(message: $MessageLite$): $Payload$ {\n");
      p->Indent();
      p->Print(
          *vars,
          "val data = serialize(message)\n"
          "val metadataBuf: $ByteBuf$ =\n"
          "if (once.compareAndSet(false, true)) {\n");
      p->Indent();
      p->Print(
          *vars,
          "$RSocketRpcMetadata$.encode($ByteBufAllocator$.DEFAULT, $service_name$.$service_field_name$, $service_name$.$method_field_name$, metadata)\n"
          );
      p->Outdent();
      p->Print("} else {\n");
      p->Indent();
      p->Print(
          *vars,
          "$Unpooled$.EMPTY_BUFFER\n"
          );
      p->Outdent();
      p->Print("}\n");
      p->Print(*vars,
         "val payload = $DefaultPayload$(data, metadataBuf)\n"
         "metadataBuf.release()\n"
         "data.release()\n"
         "return payload\n"
         );
      p->Outdent();
      p->Print("}\n");
      p->Outdent();
      if (server_streaming) {
        p->Print(
            *vars,
            "})).map(deserializer($output_type$.parser())).compose($lower_method_name$Trace(map))\n");
      } else {
        p->Print(
            *vars,
            "})).map(deserializer($output_type$.parser())).compose($lower_method_name$Trace(map)).firstOrError()\n");
      }
      p->Outdent();
      p->Outdent();
      p->Print("}\n\n");
    } else {
      // Server streaming or simple RPC
      p->Print(
          *vars,
          "val map: $Map$<String, String> = $HashMap$()\n"
          );
      p->Indent();

      if (server_streaming) {
        p->Print(
            *vars,
            "return $Flowable$.defer {\n");
        p->Indent();
        p->Print(
            *vars,
            "val tracingMetadata = $RSocketRpcTracing$.mapToByteBuf($ByteBufAllocator$.DEFAULT, map)\n"
            "val metadataBuf = $RSocketRpcMetadata$.encode($ByteBufAllocator$.DEFAULT, $service_name$.$service_field_name$, $service_name$.$method_field_name$, tracingMetadata, metadata)\n"
            "val data = serialize(message)\n"
            "val payload = $DefaultPayload$(data, metadataBuf)\n"
            "tracingMetadata.release()\n"
            "metadataBuf.release()\n"
            "data.release()\n"
            "rSocket.requestStream(payload)\n");
        p->Outdent();
        p->Print("}\n");
        p->Outdent();
        p->Print(
            *vars,
            ".map(deserializer($output_type$.parser())).compose($lower_method_name$Trace(map))\n");
      } else {
        if (options.fire_and_forget()) {
          p->Print(
              *vars,
              "return $Completable$.defer {\n");

          p->Indent();
          p->Print(
              *vars,
              "val tracingMetadata = $RSocketRpcTracing$.mapToByteBuf($ByteBufAllocator$.DEFAULT, map)\n"
              "val metadataBuf = $RSocketRpcMetadata$.encode($ByteBufAllocator$.DEFAULT, $service_name$.$service_field_name$, $service_name$.$method_field_name$, tracingMetadata, metadata)\n"
              "val data = serialize(message)\n"
              "val payload = $DefaultPayload$(data, metadataBuf)\n"
              "tracingMetadata.release()\n"
              "metadataBuf.release()\n"
              "data.release()\n"
              "rSocket.fireAndForget(payload)\n");
          p->Outdent();
          p->Print("}\n");
          p->Outdent();
          p->Print(
              *vars,
              ".toFlowable<$output_type$>().compose($lower_method_name$Trace(map)).ignoreElements()\n");
        } else {
          p->Print(
              *vars,
              "return $Single$.defer {\n");
          p->Indent();
          p->Print(
              *vars,
              "val tracingMetadata = $RSocketRpcTracing$.mapToByteBuf($ByteBufAllocator$.DEFAULT, map)\n"
              "val metadataBuf = $RSocketRpcMetadata$.encode($ByteBufAllocator$.DEFAULT, $service_name$.$service_field_name$, $service_name$.$method_field_name$, tracingMetadata, metadata)\n"
              "val data = serialize(message)\n"
              "val payload = $DefaultPayload$(data, metadataBuf)\n"
              "tracingMetadata.release()\n"
              "metadataBuf.release()\n"
              "data.release()\n"
              "rSocket.requestResponse(payload)\n");
          p->Outdent();
          p->Print("}\n");
          p->Outdent();
          p->Print(
              *vars,
              ".map(deserializer($output_type$.parser())).toFlowable().compose($lower_method_name$Trace(map)).firstOrError()\n");
        }
      }

      p->Outdent();
      p->Print("}\n\n");
    }
  }

  // Serialize method
  p->Print(
  *vars,
  "private fun serialize(message: $MessageLite$):$ByteBuf$ {\n");
  p->Indent();
  p->Print(
    *vars,
    "val length = message.serializedSize\n"
    "val byteBuf = $ByteBufAllocator$.DEFAULT.buffer(length)\n");
  p->Print("try {\n");
  p->Indent();
  p->Print(
    *vars,
    "message.writeTo($CodedOutputStream$.newInstance(byteBuf.nioBuffer(0, length)))\n"
    "byteBuf.writerIndex(length)\n"
    "return byteBuf\n");
  p->Outdent();
  p->Print("} catch (t: Throwable) {\n");
  p->Indent();
  p->Print(
    "byteBuf.release()\n"
    "throw RuntimeException(t)\n");
  p->Outdent();
  p->Print("}\n");
  p->Outdent();
  p->Print("}\n\n");

  // Deserializer
  p->Print(
      *vars,
      "private fun <T> deserializer(parser: $Parser$<T>): ($Payload$) -> T =\n");
  p->Indent();
  p->Print(
      *vars,
      "{ payload -> parser.parseFrom($CodedInputStream$.newInstance(payload.data)) }\n");
  p->Outdent();
  p->Print("}\n");
}

static void PrintServer(const ServiceDescriptor* service,
                        std::map<string, string>* vars,
                        Printer* p,
                        ProtoFlavor flavor,
                        bool disable_version) {
  (*vars)["service_name"] = service->name();
  (*vars)["service_field_name"] = ServiceFieldName(service);
  (*vars)["file_name"] = service->file()->name();
  (*vars)["server_class_name"] = ServerClassName(service);
  (*vars)["rsocket_rpc_version"] = "";
  (*vars)["version"] = "";
  #ifdef rsocket_rpc_version
  if (!disable_version) {
    (*vars)["rsocket_rpc_version"] = " (version " XSTR(rsocket_rpc_version) ")";
    (*vars)["version"] = XSTR(rsocket_rpc_version);
  }
  #endif
  p->Print(
      *vars,
      "@$Generated$(\n"
      "    value = [\"by RSocket RPC proto compiler$rsocket_rpc_version$\"],\n"
      "    comments = \"Source: $file_name$\")\n"
      "@$RSocketRpcGenerated$(\n"
      "    type = $RSocketRpcResourceType$.SERVICE,\n"
      "    idlClass = $service_name$::class)\n"
      "@$Named$(\n"
      "    value =\"$server_class_name$\")\n"
      "class $server_class_name$ @$Inject$ constructor(\n");
  p->Indent();
  p->Print(
      *vars,
      "private val serviceImpl: $service_name$,\n"
      "tracer: $Optional$.Optional<$Tracer$> = $Optional$.None)\n"
      ": $AbstractRSocketService$() {\n"
      "private val tracer: $Tracer$?\n");

  // Tracing
  for (int i = 0; i < service->method_count(); ++i) {
    const MethodDescriptor* method = service->method(i);
    const RSocketMethodOptions options = method->options().GetExtension(io::rsocket::rpc::options);
    (*vars)["lower_method_name"] = LowerMethodName(method);
    bool client_streaming = method->client_streaming();
    bool server_streaming = method->server_streaming();

    if (server_streaming) {
      p->Print(
          *vars,
          "private val $lower_method_name$Trace: ($SpanContext$?) -> io.reactivex.FlowableTransformer<$Payload$, $Payload$>\n");
    } else if (client_streaming) {
      p->Print(
          *vars,
          "private val $lower_method_name$Trace: ($SpanContext$?) -> io.reactivex.FlowableTransformer<$Payload$, $Payload$>\n");
    } else {
      const Descriptor* output_type = method->output_type();
      if (options.fire_and_forget()) {
        p->Print(
            *vars,
            "private val $lower_method_name$Trace: ($SpanContext$?) -> io.reactivex.FlowableTransformer<$Empty$, $Empty$>\n");
      } else {
        p->Print(
            *vars,
            "private val $lower_method_name$Trace: ($SpanContext$?) -> io.reactivex.FlowableTransformer<$Payload$, $Payload$>\n");
      }
    }
  }
p->Print("\ninit {\n");
p->Indent();
p->Print("when(tracer) {\n");
p->Indent();
// if tracing present {
    p->Print(
        *vars,
        "is $Optional$.None -> {\n"
    );
    p->Indent();
    p->Print(
        *vars,
        "this.tracer = null\n"
    );
    for (int i = 0; i < service->method_count(); ++i) {
      const MethodDescriptor* method = service->method(i);
      (*vars)["lower_method_name"] = LowerMethodName(method);

      p->Print(
         *vars,
         "this.$lower_method_name$Trace = $RSocketRpcTracing$.traceAsChild()\n");
    }

    // } else tracing not present {
    p->Outdent();
    p->Print("}\n");
    p->Print(
            *vars,
            "is $Optional$.Some -> {\n"
        );
    p->Indent();
    p->Print(
        *vars,
        "this.tracer = tracer.value\n"
    );
    for (int i = 0; i < service->method_count(); ++i) {
      const MethodDescriptor* method = service->method(i);
      (*vars)["lower_method_name"] = LowerMethodName(method);
      (*vars)["method_field_name"] = MethodFieldName(method);

      p->Print(
          *vars,
          "this.$lower_method_name$Trace = $RSocketRpcTracing$.traceAsChild(\n");
      p->Indent();
      p->Print(
          *vars,
          "this.tracer,\n"
           "$service_name$.$method_field_name$,\n"
           "$Tag$.of(\"rsocket.service\", $service_name$.$service_field_name$),\n"
           "$Tag$.of(\"rsocket.type\", \"server\"),\n"
           "$Tag$.of(\"rsocket.version\", \"$version$\")\n");
      p->Outdent();
      p->Print(")\n");
   }
  p->Outdent();
  p->Print("}\n");
  p->Outdent();
  p->Print("}\n");
  p->Outdent();
  p->Print("}\n\n");

  p->Print(
      *vars,
      "override val service: String = $service_name$.$service_field_name$\n");
  p->Print("\n");

  p->Print(
        *vars,
        "override val serviceClass = serviceImpl.javaClass\n");
  p->Print("\n");

  std::vector<const MethodDescriptor*> fire_and_forget;
  std::vector<const MethodDescriptor*> request_response;
  std::vector<const MethodDescriptor*> request_stream;
  std::vector<const MethodDescriptor*> request_channel;

  for (int i = 0; i < service->method_count(); ++i) {
    const MethodDescriptor* method = service->method(i);
    const RSocketMethodOptions options = method->options().GetExtension(io::rsocket::rpc::options);
    bool client_streaming = method->client_streaming();
    bool server_streaming = method->server_streaming();

    if (client_streaming) {
      request_channel.push_back(method);
    } else if (server_streaming) {
      request_stream.push_back(method);
    } else {
      if (options.fire_and_forget()) {
        fire_and_forget.push_back(method);
      } else {
        request_response.push_back(method);
      }
    }
  }

  // Fire and forget
  p->Print(
      *vars,
      "override fun fireAndForget(payload: $Payload$):$Completable$ {\n");
  p->Indent();
  if (fire_and_forget.empty()) {
    p->Print(
        *vars,
        "return $Completable$.error(UnsupportedOperationException(\"Fire and forget not implemented.\"))\n");
  } else {
    p->Print(
        *vars,
        "try {\n");
    p->Indent();
    p->Print(
        *vars,
        "val metadata = $Unpooled$.wrappedBuffer(payload.metadata)\n"
        "val spanContext = $RSocketRpcTracing$.deserializeTracingMetadata(tracer, metadata)\n"
        "val method = $RSocketRpcMetadata$.getMethod(metadata)\n"
        "return when (method) {\n");
    p->Indent();
    for (vector<const MethodDescriptor*>::iterator it = fire_and_forget.begin(); it != fire_and_forget.end(); ++it) {
      const MethodDescriptor* method = *it;
      (*vars)["input_type"] = MessageFullKotlinName(method->input_type());
      (*vars)["lower_method_name"] = LowerMethodName(method);
      (*vars)["method_field_name"] = MethodFieldName(method);
      p->Indent();
      p->Print(
          *vars,
          "$service_name$.$method_field_name$ -> {\n");
      p->Indent();
      p->Print(
          *vars,
          "val inputStream = $CodedInputStream$.newInstance(payload.data)\n"
          "serviceImpl.$lower_method_name$($input_type$.parseFrom(inputStream), metadata).toFlowable<$Empty$>().compose($lower_method_name$Trace(spanContext)).ignoreElements()\n");
      p->Outdent();
      p->Print("}\n");
      p->Outdent();
    }
    p->Print(
        *vars,
        "else -> $Completable$.error(UnsupportedOperationException())");
    p->Outdent();
    p->Print("}\n");
    p->Outdent();
    p->Print("} catch (t: Throwable) {\n");
    p->Indent();
    p->Print(
        *vars,
        "return $Completable$.error(t)\n");
    p->Outdent();
    p->Print("}\n");
  }
  p->Outdent();
  p->Print("}\n\n");

  // Request-Response
  p->Print(
      *vars,
      "override fun requestResponse(payload: $Payload$): $Single$<$Payload$> {\n");
  p->Indent();
  if (request_response.empty()) {
    p->Print(
        *vars,
        "return $Single$.error(UnsupportedOperationException(\"Request-Response not implemented.\"))\n");
  } else {
    p->Print(
        *vars,
        "try {\n");
    p->Indent();
    p->Print(
        *vars,
        "val metadata = $Unpooled$.wrappedBuffer(payload.metadata)\n"
        "val spanContext = $RSocketRpcTracing$.deserializeTracingMetadata(tracer, metadata)\n"
        "val method = $RSocketRpcMetadata$.getMethod(metadata)\n"
        "return when (method) {\n");

    p->Indent();
    for (vector<const MethodDescriptor*>::iterator it = request_response.begin(); it != request_response.end(); ++it) {
      const MethodDescriptor* method = *it;
      (*vars)["input_type"] = MessageFullKotlinName(method->input_type());
      (*vars)["output_type"] = MessageFullKotlinName(method->output_type());
      (*vars)["lower_method_name"] = LowerMethodName(method);
      (*vars)["method_field_name"] = MethodFieldName(method);
      p->Print(
          *vars,
          "$service_name$.$method_field_name$ -> {\n");
      p->Indent();
      p->Print(
          *vars,
          "val inputStream = $CodedInputStream$.newInstance(payload.data)\n"
          "serviceImpl.$lower_method_name$($input_type$.parseFrom(inputStream), metadata).map(serializer).toFlowable().compose($lower_method_name$Trace(spanContext)).firstOrError()\n");
      p->Outdent();
      p->Print("}\n");
    }
    p->Print(
        *vars,
        "else -> $Single$.error(UnsupportedOperationException())\n");
     p->Outdent();
    p->Print("}\n");
    p->Outdent();
    p->Print("} catch (t: Throwable) {\n");
    p->Indent();
    p->Print(
        *vars,
        "return $Single$.error(t)\n");
    p->Outdent();
    p->Print("}\n");
  }
  p->Outdent();
  p->Print("}\n\n");

  // Request-Stream
  p->Print(
      *vars,
      "override fun requestStream(payload: $Payload$): $Flowable$<$Payload$> {\n");
  p->Indent();
  if (request_stream.empty()) {
    p->Print(
        *vars,
        "return $Flowable$.error(UnsupportedOperationException(\"Request-Stream not implemented.\"))\n");
  } else {
    p->Print(
        *vars,
        "try {\n");
    p->Indent();
    p->Print(
        *vars,
        "val metadata = $Unpooled$.wrappedBuffer(payload.metadata)\n"
        "val spanContext = $RSocketRpcTracing$.deserializeTracingMetadata(tracer, metadata)\n"
        "val method = $RSocketRpcMetadata$.getMethod(metadata)\n"
        "return when (method) {\n");
    p->Indent();
    for (vector<const MethodDescriptor*>::iterator it = request_stream.begin(); it != request_stream.end(); ++it) {
      const MethodDescriptor* method = *it;
      (*vars)["input_type"] = MessageFullKotlinName(method->input_type());
      (*vars)["output_type"] = MessageFullKotlinName(method->output_type());
      (*vars)["lower_method_name"] = LowerMethodName(method);
      (*vars)["method_field_name"] = MethodFieldName(method);
      p->Print(
          *vars,
          "$service_name$.$method_field_name$ -> {\n");
      p->Indent();
      p->Print(
          *vars,
          "val inputStream = $CodedInputStream$.newInstance(payload.data)\n"
          "serviceImpl.$lower_method_name$($input_type$.parseFrom(inputStream), metadata).map(serializer).compose($lower_method_name$Trace(spanContext))\n");
      p->Outdent();
      p->Print("}\n");
    }
    p->Print(
        *vars,
        "else -> $Flowable$.error(UnsupportedOperationException())\n");
    p->Print("}\n");
    p->Outdent();
    p->Print("} catch (t: Throwable) {\n");
    p->Indent();
    p->Print(
        *vars,
        "return $Flowable$.error(t)\n");
    p->Outdent();
    p->Print("}\n");
  }
  p->Outdent();
  p->Print("}\n\n");

  // Request-Channel
  p->Print(
      *vars,
      "override fun requestChannel(payload: $Payload$, publisher: $Flowable$<$Payload$>): $Flowable$<$Payload$> {\n");
  p->Indent();
  if (request_channel.empty()) {
    p->Print(
        *vars,
        "return $Flowable$.error(UnsupportedOperationException(\"Request-Channel not implemented.\"))\n");
  } else {
    p->Print(
        *vars,
        "try {\n");
    p->Indent();
    p->Print(
        *vars,
        "val metadata = $Unpooled$.wrappedBuffer(payload.metadata)\n"
        "val spanContext = $RSocketRpcTracing$.deserializeTracingMetadata(tracer, metadata)\n"
        "val method = $RSocketRpcMetadata$.getMethod(metadata)\n"
        "return when (method) {\n");
    p->Indent();
    for (vector<const MethodDescriptor*>::iterator it = request_channel.begin(); it != request_channel.end(); ++it) {
      const MethodDescriptor* method = *it;
      (*vars)["input_type"] = MessageFullKotlinName(method->input_type());
      (*vars)["output_type"] = MessageFullKotlinName(method->output_type());
      (*vars)["lower_method_name"] = LowerMethodName(method);
      (*vars)["method_field_name"] = MethodFieldName(method);
      p->Print(
          *vars,
          "$service_name$.$method_field_name$ -> {\n");
      p->Indent();
      p->Print(
          *vars,
          "val message: $Flowable$<$input_type$> =\n");
      p->Indent();
      p->Print(
          *vars,
          "publisher.startWith(payload).map(deserializer($input_type$.parser()))\n");
      p->Outdent();
      if (method->server_streaming()) {
        p->Print(
            *vars,
            "return serviceImpl.$lower_method_name$(message, metadata).map(serializer).compose($lower_method_name$Trace(spanContext))\n");
      } else {
        p->Print(
            *vars,
            "return serviceImpl.$lower_method_name$(message, metadata).map(serializer).toFlowable().compose($lower_method_name$Trace(spanContext))\n");
      }

      p->Outdent();
      p->Print("}\n");
    }
    p->Print(
        *vars,
        "else  -> $Flowable$.error(UnsupportedOperationException())\n");
    p->Print("}\n");
    p->Outdent();
    p->Print("} catch (t: Throwable) {\n");
    p->Indent();
    p->Print(
        *vars,
        "return $Flowable$.error(t)\n");
    p->Outdent();
    p->Print("}\n");
  }
  p->Outdent();
  p->Print("}\n\n");

  p->Print(
      *vars,
      "override fun requestChannel(payloads: $Publisher$<$Payload$>): $Flowable$<$Payload$> {\n");
  p->Indent();
  if (request_channel.empty()) {
    p->Print(
        *vars,
        "return $Flowable$.error(UnsupportedOperationException(\"Request-Channel not implemented.\"))\n");
  } else {
    p->Print(
        *vars,
        "return $StreamSplitter$.split(payloads)\n");
    p->Indent();
    p->Print(
        *vars,
        ".flatMap { (head, tail) -> requestChannel(head, tail) }\n");
  }
  p->Outdent();
  p->Print("}\n\n");

  p->Print("companion object {\n");
  p->Indent();

  // Serializer
  p->Print(
      *vars,
      "private val serializer:($MessageLite$) -> $Payload$ = { message ->\n");
  p->Indent();
  p->Print(
    *vars,
    "val length = message.serializedSize\n"
    "val byteBuf = $ByteBufAllocator$.DEFAULT.buffer(length)\n");
  p->Print("try {\n");
  p->Indent();
  p->Print(
    *vars,
    "message.writeTo($CodedOutputStream$.newInstance(byteBuf.nioBuffer(0, length)))\n"
    "byteBuf.writerIndex(length)\n"
    "$DefaultPayload$(byteBuf, $Unpooled$.EMPTY_BUFFER)\n");
  p->Outdent();
  p->Print("} catch (t: Throwable) {\n");
  p->Indent();
  p->Print(
    "throw RuntimeException(t)\n");
  p->Outdent();
  p->Print("} finally {\n");
  p->Indent();
  p->Print("byteBuf.release()\n");
  p->Outdent();
  p->Print("}\n");
  p->Outdent();
  p->Print("}\n\n");
  p->Outdent();

  // Deserializer
  p->Print(
      *vars,
      "private fun <T> deserializer(parser: $Parser$<T>):($Payload$) -> T {\n");
  p->Indent();
  p->Print(
      *vars,
      "return { payload -> \n");
  p->Indent();
  p->Print(
      *vars,
      "try {\n");
  p->Indent();
  p->Print(
      *vars,
      "val inputStream = $CodedInputStream$.newInstance(payload.data)\n"
      "parser.parseFrom(inputStream)\n");
  p->Outdent();
  p->Print("} catch (t: Throwable) {\n");
  p->Indent();
  p->Print(
      *vars,
      "throw RuntimeException(t)\n");
  p->Outdent();
  p->Print("}\n");
  p->Outdent();
  p->Print("}\n");
  p->Outdent();
  p->Print("}\n");
  p->Outdent();
  p->Print("}\n");
  p->Outdent();
  p->Print("}\n");
}

void GenerateInterface(const ServiceDescriptor* service,
                       google::protobuf::io::ZeroCopyOutputStream* out,
                       ProtoFlavor flavor,
                       bool disable_version) {
  // All non-generated classes must be referred by fully qualified names to
  // avoid collision with generated classes.
  std::map<string, string> vars;
  vars["Flowable"] = "io.reactivex.Flowable";
  vars["Single"] = "io.reactivex.Single";
  vars["Publisher"] = "org.reactivestreams.Publisher";
  vars["Generated"] = "javax.annotation.Generated";
  vars["ByteBuf"] = "io.netty.buffer.ByteBuf";
  vars["Completable"] = "io.reactivex.Completable";
  vars["Empty"] = "com.google.protobuf.Empty";

  Printer printer(out, '$');
  string package_name = ServiceKotlinPackage(service->file());
  if (!package_name.empty()) {
    printer.Print(
        "package $package_name$\n\n",
        "package_name", package_name);
  }

  // Package string is used to fully qualify method names.
  vars["Package"] = service->file()->package();
  if (!vars["Package"].empty()) {
    vars["Package"].append(".");
  }
  PrintInterface(service, &vars, &printer, flavor, disable_version);
}

void GenerateClient(const ServiceDescriptor* service,
                    google::protobuf::io::ZeroCopyOutputStream* out,
                    ProtoFlavor flavor,
                    bool disable_version) {
  // All non-generated classes must be referred by fully qualified names to
  // avoid collision with generated classes.
  std::map<string, string> vars;
  vars["Flowable"] = "io.reactivex.Flowable";
  vars["Single"] = "io.reactivex.Single";
  vars["Completable"] = "io.reactivex.Completable";
  vars["from"] = "fromPublisher";
  vars["Function"] = "io.reactivex.functions.Function";
  vars["Supplier"] = "java.util.concurrent.Callable";
  vars["AtomicBoolean"] = "java.util.concurrent.atomic.AtomicBoolean";
  vars["Override"] = "java.lang.Override";
  vars["Publisher"] = "org.reactivestreams.Publisher";
  vars["Generated"] = "javax.annotation.Generated";
  vars["RSocketRpcGenerated"] = "io.rsocket.rpc.kotlin.annotations.Generated";
  vars["RSocketRpcResourceType"] = "io.rsocket.rpc.kotlin.annotations.ResourceType";
  vars["RSocket"] = "io.rsocket.kotlin.RSocket";
  vars["Payload"] = "io.rsocket.kotlin.Payload";
  vars["DefaultPayload"] = "io.rsocket.kotlin.DefaultPayload";
  vars["ByteBuf"] = "io.netty.buffer.ByteBuf";
  vars["ByteBufAllocator"] = "io.netty.buffer.ByteBufAllocator";
  vars["Unpooled"] = "io.netty.buffer.Unpooled";
  vars["ByteBuffer"] = "java.nio.ByteBuffer";
  vars["CodedInputStream"] = "com.google.protobuf.CodedInputStream";
  vars["CodedOutputStream"] = "com.google.protobuf.CodedOutputStream";
  vars["RSocketRpcMetadata"] = "io.rsocket.rpc.kotlin.frames.Metadata";
  vars["RSocketRpcMetrics"] = "io.rsocket.rpc.kotlin.metrics.Metrics";
  vars["MeterRegistry"] = "io.micrometer.core.instrument.MeterRegistry";
  vars["MessageLite"] = "com.google.protobuf.MessageLite";
  vars["Parser"] = "com.google.protobuf.Parser";
  vars["RSocketRpcGeneratedMethod"] = "io.rsocket.rpc.kotlin.annotations.GeneratedMethod";
  vars["RSocketRpcTracing"] = "io.rsocket.rpc.kotlin.tracing.Tracing";
  vars["Tag"] = "io.rsocket.rpc.kotlin.tracing.Tag";
  vars["Tracer"] = "io.opentracing.Tracer";
  vars["Map"] = "Map";
  vars["HashMap"] = "java.util.HashMap";
  vars["Supplier"] = "java.util.concurrent.Callable";
  vars["Empty"] = "com.google.protobuf.Empty";
  vars["Optional"] = "com.gojuno.koptional";

  Printer printer(out, '$');
  string package_name = ServiceKotlinPackage(service->file());
  if (!package_name.empty()) {
    printer.Print(
        "package $package_name$\n\n",
        "package_name", package_name);
  }

  // Package string is used to fully qualify method names.
  vars["Package"] = service->file()->package();
  if (!vars["Package"].empty()) {
    vars["Package"].append(".");
  }
  PrintClient(service, &vars, &printer, flavor, disable_version);
}

void GenerateServer(const ServiceDescriptor* service,
                    google::protobuf::io::ZeroCopyOutputStream* out,
                    ProtoFlavor flavor,
                    bool disable_version) {
  // All non-generated classes must be referred by fully qualified names to
  // avoid collision with generated classes.
  std::map<string, string> vars;
  vars["Flowable"] = "io.reactivex.Flowable";
  vars["Single"] = "io.reactivex.Single";
  vars["Completable"] = "io.reactivex.Completable";
  vars["from"] = "from";
  vars["flux"] = "flux";
  vars["flatMap"] = "flatMapPublisher";
  vars["Function"] = "io.reactivex.functions.Function";
  vars["Supplier"] = "io.rsocket.rpc.kotlin.util.Supplier";
  vars["BiFunction"] = "io.rsocket.rpc.kotlin.util.BiFunction";
  vars["Override"] = "java.lang.Override";
  vars["Publisher"] = "org.reactivestreams.Publisher";
  vars["Generated"] = "javax.annotation.Generated";
  vars["RSocketRpcGenerated"] = "io.rsocket.rpc.kotlin.annotations.Generated";
  vars["RSocket"] = "io.rsocket.kotlin.RSocket";
  vars["Payload"] = "io.rsocket.kotlin.Payload";
  vars["DefaultPayload"] = "io.rsocket.kotlin.DefaultPayload";
  vars["AbstractRSocketService"] = "io.rsocket.rpc.kotlin.AbstractRSocketService";
  vars["RSocketRpcMetadata"] = "io.rsocket.rpc.kotlin.frames.Metadata";
  vars["RSocketRpcMetrics"] = "io.rsocket.rpc.kotlin.metrics.Metrics";
  vars["MeterRegistry"] = "io.micrometer.core.instrument.MeterRegistry";
  vars["ByteBufs"] = "io.rsocket.rpc.kotlin.util.ByteBufs";
  vars["ByteBuf"] = "io.netty.buffer.ByteBuf";
  vars["ByteBuffer"] = "java.nio.ByteBuffer";
  vars["ByteBufAllocator"] = "io.netty.buffer.ByteBufAllocator";
  vars["CodedInputStream"] = "com.google.protobuf.CodedInputStream";
  vars["CodedOutputStream"] = "com.google.protobuf.CodedOutputStream";
  vars["MessageLite"] = "com.google.protobuf.MessageLite";
  vars["Parser"] = "com.google.protobuf.Parser";
  vars["Optional"] = "com.gojuno.koptional";
  vars["Inject"] = "javax.inject.Inject";
  vars["Unpooled"] = "io.netty.buffer.Unpooled";
  vars["Named"] = "javax.inject.Named";
  vars["RSocketRpcResourceType"] = "io.rsocket.rpc.kotlin.annotations.ResourceType";
  vars["RSocketRpcTracing"] = "io.rsocket.rpc.kotlin.tracing.Tracing";
  vars["Tag"] = "io.rsocket.rpc.kotlin.tracing.Tag";
  vars["SpanContext"] = "io.opentracing.SpanContext";
  vars["Tracer"] = "io.opentracing.Tracer";
  vars["Empty"] = "com.google.protobuf.Empty";
  vars["StreamSplitter"] = "io.rsocket.rpc.kotlin.util.StreamSplitter";

  Printer printer(out, '$');
  string package_name = ServiceKotlinPackage(service->file());
  if (!package_name.empty()) {
    printer.Print(
        "package $package_name$;\n\n",
        "package_name", package_name);
  }

  // Package string is used to fully qualify method names.
  vars["Package"] = service->file()->package();
  if (!vars["Package"].empty()) {
    vars["Package"].append(".");
  }
  PrintServer(service, &vars, &printer, flavor, disable_version);
}

string ServiceKotlinPackage(const FileDescriptor* file) {
  string result = google::protobuf::compiler::java::ClassName(file);
  size_t last_dot_pos = result.find_last_of('.');
  if (last_dot_pos != string::npos) {
    result.resize(last_dot_pos);
  } else {
    result = "";
  }
  return result;
}

string ClientClassName(const google::protobuf::ServiceDescriptor* service) {
  return service->name() + "Client";
}

string ServerClassName(const google::protobuf::ServiceDescriptor* service) {
  return service->name() + "Server";
}

}  // namespace kotlin_rsocket_rpc_generator
