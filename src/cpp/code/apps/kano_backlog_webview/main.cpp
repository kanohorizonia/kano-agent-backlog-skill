#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>

#include <drogon/drogon.h>

#include "assets/index_html.hpp"
#include "assets/kob_ui_js.hpp"
#include "kano/backlog_core/process/noninteractive_errors.hpp"
#include "KanoBacklog.BacklogWebviewService.hpp"

namespace {

std::filesystem::path ResolveProductsRoot(int argc, char** argv) {
  std::filesystem::path defaultRoot = "_kano/backlog/products";

  if (const char* envRoot = std::getenv("KANO_BACKLOG_PRODUCTS_ROOT"); envRoot != nullptr) {
    if (std::strlen(envRoot) > 0) {
      defaultRoot = envRoot;
    }
  }

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--backlog-root" && (i + 1) < argc) {
      return argv[i + 1];
    }
  }

  return defaultRoot;
}

uint16_t ResolvePort(int argc, char** argv) {
  uint16_t port = 8787;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--port" && (i + 1) < argc) {
      port = static_cast<uint16_t>(std::stoi(argv[i + 1]));
      return port;
    }
  }

  if (const char* envPort = std::getenv("KANO_WEBVIEW_PORT"); envPort != nullptr) {
    if (std::strlen(envPort) > 0) {
      port = static_cast<uint16_t>(std::stoi(envPort));
    }
  }
  return port;
}

std::string ResolveHost(int argc, char** argv) {
  std::string host = "127.0.0.1";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--host" && (i + 1) < argc) {
      return argv[i + 1];
    }
  }

  if (const char* envHost = std::getenv("KANO_WEBVIEW_HOST"); envHost != nullptr) {
    if (std::strlen(envHost) > 0) {
      host = envHost;
    }
  }
  return host;
}

}  // namespace

int main(int argc, char** argv) {
  kano::backlog_core::ConfigureNoninteractiveErrorHandling();

  const auto productsRoot = ResolveProductsRoot(argc, argv);
  const auto host = ResolveHost(argc, argv);
  const auto port = ResolvePort(argc, argv);

  kano::backlog::webview::BacklogWebviewService service(productsRoot);

  auto appendMeta = [&](const drogon::HttpRequestPtr&, Json::Value& body) {
    body["meta"]["products_root"] = service.GetProductsRoot().generic_string();
  };

  kano::backlog::webview::RegisterBacklogWebviewRoutes(service, appendMeta);

  drogon::app().registerHandler(
      "/assets/kob-ui.js",
      [](const drogon::HttpRequestPtr&,
         std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setStatusCode(drogon::k200OK);
        response->setContentTypeString("application/javascript; charset=utf-8");
        response->setBody(std::string(kano::backlog::webview::assets::KobUiJs()));
        callback(response);
      },
      {drogon::Get});

  drogon::app().registerHandler(
      "/",
      [](const drogon::HttpRequestPtr&,
         std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setStatusCode(drogon::k200OK);
        response->setContentTypeCode(drogon::CT_TEXT_HTML);
        response->setBody(kano::backlog::webview::assets::IndexHtml());
        callback(response);
      },
      {drogon::Get});

  drogon::app().setLogLevel(trantor::Logger::kWarn);
  drogon::app().setThreadNum(1);
  drogon::app().addListener(host, port);
  drogon::app().run();
  return 0;
}
