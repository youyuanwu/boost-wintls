//
// Copyright (c) 2020 Kasper Laudrup (laudrup at stacktrace dot dk)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "async_echo_client.hpp"
#include "async_echo_server.hpp"
#include "certificate.hpp"
#include "tls_record.hpp"
#include "unittest.hpp"

#include <boost/wintls.hpp>
#include "asio_ssl_server_stream.hpp"
#include "asio_ssl_client_stream.hpp"
#include "wintls_client_stream.hpp"
#include "wintls_server_stream.hpp"

#include <boost/system/error_code.hpp>
#include <boost/beast/_experimental/test/stream.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <iostream>

// TODO: remove
#define MY_ENCODING_TYPE  (PKCS_7_ASN_ENCODING | X509_ASN_ENCODING)
PCCERT_CONTEXT findCert(){

    HCERTSTORE  hSystemStore;              // System store handle
    PCCERT_CONTEXT  pDesiredCert = NULL;
    // PCCERT_CONTEXT  pCertContext;

    hSystemStore = CertOpenStore(
     CERT_STORE_PROV_SYSTEM, // System store will be a 
                             // virtual store
     0,                      // Encoding type not needed 
                             // with this PROV
     NULL,                   // Accept the default HCRYPTPROV
     CERT_SYSTEM_STORE_CURRENT_USER,
                             // Set the system store location in the
                             // registry
     L"MY");                 // Could have used other predefined 
                             // system stores
                             // including Trust, CA, or Root
    if(hSystemStore)
    {
    printf("Opened the MY system store. \n");
    }
    else
    {

        printf("Cannot Opened the MY system store. \n");
        return pDesiredCert;
    }

    // find my cert
    pDesiredCert=CertFindCertificateInStore(
      hSystemStore,
      MY_ENCODING_TYPE,             // Use X509_ASN_ENCODING
      0,                            // No dwFlags needed 
      CERT_FIND_SUBJECT_STR,        // Find a certificate with a
                                    // subject that matches the 
                                    // string in the next parameter
      L"Patti Fuller",              // The Unicode string to be found
                                    // in a certificate's subject
      NULL);                        // NULL for the first call to the
                                    // function 
                                    // In all subsequent
                                    // calls, it is the last pointer
                                    // returned by the function
    if(pDesiredCert)
    {
        printf("The desired certificate was found. \n");
    }
    else
    {
        printf("The desired certificate was not found. \n");
        return pDesiredCert;
    }

    if(hSystemStore)
        CertCloseStore(
            hSystemStore, 
            0);

    return pDesiredCert;
}

TEST_CASE("certificates") {
  using namespace std::string_literals;

  boost::wintls::context client_ctx(boost::wintls::method::system_default);

  boost::asio::ssl::context server_ctx(boost::asio::ssl::context::tls_server);
  server_ctx.use_certificate_chain(net::buffer(test_certificate));
  server_ctx.use_private_key(net::buffer(test_key), boost::asio::ssl::context::pem);

  net::io_context io_context;
  boost::wintls::stream<test_stream> client_stream(io_context, client_ctx);
  boost::asio::ssl::stream<test_stream> server_stream(io_context, server_ctx);

  client_stream.next_layer().connect(server_stream.next_layer());

  SECTION("invalid certificate data") {
    // TODO: Instead of returning an error when given a null pointer
    // or other easily detectable invalid input, the Windows crypto
    // libraries cause the Windows equivalent of a segfault. This is
    // pretty consistent with the rest of the Windows API though.
    //
    // Figure out a way to generate invalid data that doesn't make the
    // test crash.
    /*
    using namespace boost::system;

    auto error = errc::make_error_code(errc::not_supported);

    CERT_INFO cert_info{};
    const CERT_CONTEXT bad_cert{
      X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
      nullptr,
      0,
      &cert_info,
      0};
    client_ctx.add_certificate_authority(&bad_cert, error);

    CHECK(error.category() == boost::system::system_category());
    CHECK(error.value() == CRYPT_E_ASN1_EOD);
    */
  }

  SECTION("no certificate validation") {
    using namespace boost::system;

    auto client_error = errc::make_error_code(errc::not_supported);
    client_stream.async_handshake(boost::wintls::handshake_type::client,
                                  [&client_error, &io_context](const boost::system::error_code& ec) {
                                    client_error = ec;
                                    io_context.stop();
                                  });

    auto server_error = errc::make_error_code(errc::not_supported);
    server_stream.async_handshake(asio_ssl::stream_base::server,
                                  [&server_error](const boost::system::error_code& ec) {
                                    server_error = ec;
                                  });
    io_context.run();
    CHECK_FALSE(client_error);
    CHECK_FALSE(server_error);
  }

  SECTION("no trusted certificate") {
    using namespace boost::system;

    client_ctx.verify_server_certificate(true);

    auto client_error = errc::make_error_code(errc::not_supported);
    client_stream.async_handshake(boost::wintls::handshake_type::client,
                                  [&client_error](const boost::system::error_code& ec) {
                                    client_error = ec;
                                  });

    auto server_error = errc::make_error_code(errc::not_supported);
    server_stream.async_handshake(asio_ssl::stream_base::server,
                                  [&server_error](const boost::system::error_code& ec) {
                                    server_error = ec;
                                  });

    io_context.run();
    CHECK(client_error.category() == boost::system::system_category());
    CHECK(client_error.value() == CERT_E_UNTRUSTEDROOT);
    CHECK_FALSE(server_error);
  }

  SECTION("trusted certificate verified") {
    using namespace boost::system;

    client_ctx.verify_server_certificate(true);

    const auto cert_ptr = x509_to_cert_context(net::buffer(test_certificate), boost::wintls::file_format::pem);
    client_ctx.add_certificate_authority(cert_ptr.get());

    auto client_error = errc::make_error_code(errc::not_supported);
    client_stream.async_handshake(boost::wintls::handshake_type::client,
                                  [&client_error, &io_context](const boost::system::error_code& ec) {
                                    client_error = ec;
                                    io_context.stop();
                                  });

    auto server_error = errc::make_error_code(errc::not_supported);
    server_stream.async_handshake(asio_ssl::stream_base::server,
                                  [&server_error](const boost::system::error_code& ec) {
                                    server_error = ec;
                                  });
    io_context.run();
    CHECK_FALSE(client_error);
    CHECK_FALSE(server_error);
  }
}

TEST_CASE("client certificates") {
  using namespace std::string_literals;

  SECTION("wintls client certificate missing with openssl server") {
    using namespace boost::system;
    wintls_client_context client_ctx;
    asio_ssl_server_context server_ctx;
    server_ctx.enable_client_verify();

    net::io_context io_context;
    boost::wintls::stream<test_stream> client_stream(io_context, client_ctx);
    boost::asio::ssl::stream<test_stream> server_stream(io_context, server_ctx);

    client_stream.next_layer().connect(server_stream.next_layer());

    auto client_error = errc::make_error_code(errc::not_supported);
    client_stream.async_handshake(boost::wintls::handshake_type::client,
                                  [&client_error](const boost::system::error_code& ec) {
                                    client_error = ec;
                                  });

    auto server_error = errc::make_error_code(errc::not_supported);
    server_stream.async_handshake(asio_ssl::stream_base::server,
                                  [&server_error](const boost::system::error_code& ec) {
                                    server_error = ec;
                                  });
    io_context.run();
    // client handshake is failed by server
    CHECK(client_error);
    // Note: error code mapping is not working, so compare string.
    CHECK(server_error.message() == "peer did not return a certificate");
  }

  SECTION("trusted wintls client certificate verified on openssl server") {
    using namespace boost::system;

    wintls_client_context client_ctx;
    client_ctx.with_test_client_cert(); // Note that if client cert is supplied, sspi will verify server cert with it.
    client_ctx.verify_server_certificate(true);

    asio_ssl_server_context server_ctx;
    server_ctx.enable_client_verify();

    net::io_context io_context;
    boost::wintls::stream<test_stream> client_stream(io_context, client_ctx);
    boost::asio::ssl::stream<test_stream> server_stream(io_context, server_ctx);

    client_stream.next_layer().connect(server_stream.next_layer());

    auto client_error = errc::make_error_code(errc::not_supported);
    client_stream.async_handshake(boost::wintls::handshake_type::client,
                                  [&client_error](const boost::system::error_code& ec) {
                                    client_error = ec;
                                  });

    auto server_error = errc::make_error_code(errc::not_supported);
    server_stream.async_handshake(asio_ssl::stream_base::server,
                                  [&server_error](const boost::system::error_code& ec) {
                                    server_error = ec;
                                  });
    io_context.run();
    CHECK_FALSE(client_error);
    CHECK_FALSE(server_error);
  }

  SECTION("trusted openssl client certificate verified on openssl server") {
    using namespace boost::system;
    asio_ssl_client_context client_ctx;
    client_ctx.with_test_client_cert();
    client_ctx.enable_server_verify();

    asio_ssl_server_context server_ctx;
    server_ctx.enable_client_verify();

    net::io_context io_context;
    boost::asio::ssl::stream<test_stream> client_stream(io_context, client_ctx);
    boost::asio::ssl::stream<test_stream> server_stream(io_context, server_ctx);

    client_stream.next_layer().connect(server_stream.next_layer());

    auto client_error = errc::make_error_code(errc::not_supported);
    client_stream.async_handshake(asio_ssl::stream_base::client,
                                  [&client_error, &io_context](const boost::system::error_code& ec) {
                                    client_error = ec;
                                  });

    auto server_error = errc::make_error_code(errc::not_supported);
    server_stream.async_handshake(asio_ssl::stream_base::server,
                                  [&server_error](const boost::system::error_code& ec) {
                                    server_error = ec;
                                  });
    io_context.run();
    CHECK_FALSE(client_error);
    CHECK_FALSE(server_error);
  }

  SECTION("trusted openssl client certificate verified on wintls server") {
    using namespace boost::system;
    asio_ssl_client_context client_ctx;
    client_ctx.with_test_client_cert();
    client_ctx.enable_server_verify();

    wintls_server_context server_ctx;
    server_ctx.enable_client_verify();

    net::io_context io_context;
    boost::asio::ssl::stream<test_stream> client_stream(io_context, client_ctx);
    boost::wintls::stream<test_stream> server_stream(io_context, server_ctx);

    client_stream.next_layer().connect(server_stream.next_layer());

    auto client_error = errc::make_error_code(errc::not_supported);
    client_stream.async_handshake(asio_ssl::stream_base::client,
                                  [&client_error, &io_context](const boost::system::error_code& ec) {
                                    client_error = ec;
                                  });

    auto server_error = errc::make_error_code(errc::not_supported);
    server_stream.async_handshake(boost::wintls::handshake_type::server,
                                  [&server_error, &io_context](const boost::system::error_code& ec) {
                                    server_error = ec;
                                  });
    io_context.run();
    CHECK_FALSE(client_error);
    CHECK_FALSE(server_error);
  }

  SECTION("openssl client missing certificate on wintls server") {
    using namespace boost::system;
    asio_ssl_client_context client_ctx;

    wintls_server_context server_ctx;
    server_ctx.enable_client_verify();

    net::io_context io_context;
    boost::asio::ssl::stream<test_stream> client_stream(io_context, client_ctx);
    boost::wintls::stream<test_stream> server_stream(io_context, server_ctx);

    client_stream.next_layer().connect(server_stream.next_layer());

    auto client_error = errc::make_error_code(errc::not_supported);
    client_stream.async_handshake(asio_ssl::stream_base::client,
                                  [&client_error, &io_context](const boost::system::error_code& ec) {
                                    client_error = ec;
                                  });

    auto server_error = errc::make_error_code(errc::not_supported);
    server_stream.async_handshake(boost::wintls::handshake_type::server,
                                  [&server_error, &io_context](const boost::system::error_code& ec) {
                                    server_error = ec;
                                  });
    io_context.run();
    CHECK_FALSE(client_error);
    CHECK(server_error.value() == SEC_E_NO_CREDENTIALS);
  }

  SECTION("trusted wintls client certificate verified on wintls server") {
    using namespace boost::system;
    wintls_client_context client_ctx;
    client_ctx.with_test_client_cert();
    client_ctx.enable_server_verify();

    wintls_server_context server_ctx;
    server_ctx.enable_client_verify();

    net::io_context io_context;
    boost::wintls::stream<test_stream> client_stream(io_context, client_ctx);
    boost::wintls::stream<test_stream> server_stream(io_context, server_ctx);

    client_stream.next_layer().connect(server_stream.next_layer());

    auto client_error = errc::make_error_code(errc::not_supported);
    client_stream.async_handshake(boost::wintls::handshake_type::client,
                                  [&client_error, &io_context](const boost::system::error_code& ec) {
                                    client_error = ec;
                                  });

    auto server_error = errc::make_error_code(errc::not_supported);
    server_stream.async_handshake(boost::wintls::handshake_type::server,
                                  [&server_error, &io_context](const boost::system::error_code& ec) {
                                    server_error = ec;
                                  });
    io_context.run();
    CHECK_FALSE(client_error);
    CHECK_FALSE(server_error);
  }
}

TEST_CASE("failing handshakes") {
  boost::wintls::context client_ctx(boost::wintls::method::system_default);
  net::io_context io_context;
  boost::wintls::stream<test_stream> client_stream(io_context, client_ctx);
  test_stream server_stream(io_context);

  client_stream.next_layer().connect(server_stream);

  SECTION("invalid server reply") {
    using namespace boost::system;

    auto error = errc::make_error_code(errc::not_supported);
    client_stream.async_handshake(boost::wintls::handshake_type::client,
                                  [&error](const boost::system::error_code& ec) {
                                    error = ec;
                                  });

    std::array<char, 1024> buffer;
    server_stream.async_read_some(net::buffer(buffer, buffer.size()),
                                  [&buffer, &server_stream](const boost::system::error_code&, std::size_t length) {
                                    tls_record rec(net::buffer(buffer, length));
                                    REQUIRE(rec.type == tls_record::record_type::handshake);
                                    auto handshake = boost::get<tls_handshake>(rec.message);
                                    REQUIRE(handshake.type == tls_handshake::handshake_type::client_hello);
                                    // Echoing the client_hello message back should cause the handshake to fail
                                    net::write(server_stream, net::buffer(buffer));
                                  });

    io_context.run();
    CHECK(error.category() == boost::system::system_category());
    CHECK(error.value() == SEC_E_ILLEGAL_MESSAGE);
  }
}

TEST_CASE("ssl/tls versions") {
  const auto value = GENERATE(values<std::pair<boost::wintls::method, tls_version>>({
        { boost::wintls::method::tlsv1, tls_version::tls_1_0 },
        { boost::wintls::method::tlsv1_client, tls_version::tls_1_0 },
        { boost::wintls::method::tlsv11, tls_version::tls_1_1 },
        { boost::wintls::method::tlsv11_client, tls_version::tls_1_1 },
        { boost::wintls::method::tlsv12, tls_version::tls_1_2 },
        { boost::wintls::method::tlsv12_client, tls_version::tls_1_2 }
      })
    );

  const auto method = value.first;
  const auto version = value.second;

  boost::wintls::context client_ctx(method);
  net::io_context io_context;
  boost::wintls::stream<test_stream> client_stream(io_context, client_ctx);
  test_stream server_stream(io_context);

  client_stream.next_layer().connect(server_stream);

  client_stream.async_handshake(boost::wintls::handshake_type::client,
                                [](const boost::system::error_code& ec) {
                                  REQUIRE(ec == net::error::eof);
                                });

  std::array<char, 1024> buffer;
  server_stream.async_read_some(net::buffer(buffer, buffer.size()),
                                [&buffer, &server_stream, &version](const boost::system::error_code&, std::size_t length) {
                                  tls_record rec(net::buffer(buffer, length));
                                  REQUIRE(rec.type == tls_record::record_type::handshake);
                                  CHECK(rec.version == version);
                                  server_stream.close();
                                  });

    io_context.run();
}
