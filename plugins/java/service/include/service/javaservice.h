#ifndef CC_SERVICE_JAVA_JAVASERVICE_H
#define CC_SERVICE_JAVA_JAVASERVICE_H

#include <memory>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/process.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <thrift/transport/TFDTransport.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>
#include <thrift/protocol/TBinaryProtocol.h>

#include <odb/database.hxx>
#include <util/odbtransaction.h>
#include <webserver/servercontext.h>

#include <LanguageService.h>
#include <JavaService.h>

#include <iostream>
#include <chrono>

namespace cc
{
namespace service
{
namespace java
{

namespace language = cc::service::language;
namespace core = cc::service::core;

using TransportException = apache::thrift::transport::TTransportException;

class JavaQueryHandler : public JavaServiceIf
{
public:
  JavaQueryHandler() {
  }

  /**
   * Creates the client interface.
   */
  void getClientInterface(int timeoutInMs_)
  {
    using Transport = apache::thrift::transport::TTransport;
    using BufferedTransport = apache::thrift::transport::TBufferedTransport;
    using Socket = apache::thrift::transport::TSocket;
    using Protocol = apache::thrift::protocol::TBinaryProtocol;
    namespace chrono = std::chrono;

    std::string host = "localhost";
    int port = 9090;

    std::shared_ptr<Transport>
      socket(new Socket(host, port));
    std::shared_ptr<Transport>
      transport(new BufferedTransport(socket));
    std::shared_ptr<Protocol>
      protocol(new Protocol(transport));

    // Redirect Thrift output into std::stringstream
    apache::thrift::GlobalOutput.setOutputFunction(
      [](const char* x) {thrift_ss << x;});

    chrono::steady_clock::time_point begin = chrono::steady_clock::now();

    while (!transport->isOpen()) {
      try {
        transport->open();
      } catch (TransportException& ex) {
        chrono::steady_clock::time_point current = chrono::steady_clock::now();
        float elapsed_time =
          chrono::duration_cast<chrono::milliseconds>(current - begin).count();

        if (elapsed_time > timeoutInMs_) {
          LOG(debug) << "Connection timeout, could not reach Java server on"
                     << host << ":" << port;
          apache::thrift::GlobalOutput.setOutputFunction(
            apache::thrift::TOutput::errorTimeWrapper);
          throw ex;
        }
      }
    }

    apache::thrift::GlobalOutput.setOutputFunction(
      apache::thrift::TOutput::errorTimeWrapper);

    LOG(info) << "[javaparser] Java server started!";

    _service.reset(new JavaServiceClient(protocol));
  }

  void getAstNodeInfo(
    language::AstNodeInfo& return_,
    const core::AstNodeId& astNodeId_) override
  {
    _service -> getAstNodeInfo(return_, astNodeId_);
  }

  void getAstNodeInfoByPosition(
    language::AstNodeInfo& return_,
    const core::FilePosition& fpos_) override
  {
    _service -> getAstNodeInfoByPosition(return_, fpos_);
  }

  void getFileRange(
    core::FileRange& return_,
    const core::AstNodeId& astNodeId_) override
  {
    _service -> getFileRange(return_, astNodeId_);
  }

  void getProperties(
    std::map<std::string, std::string>& return_,
    const core::AstNodeId& astNodeId_) override
  {
    _service -> getProperties(return_, astNodeId_);
  }

  void getDocumentation(
    std::string& return_,
    const core::AstNodeId& astNodeId_) override
  {
    _service -> getDocumentation(return_, astNodeId_);
  }

  void getReferenceTypes(
    std::map<std::string, std::int32_t>& return_,
    const core::AstNodeId& astNodeId_) override
  {
    _service -> getReferenceTypes(return_, astNodeId_);
  }

  std::int32_t getReferenceCount(
    const core::AstNodeId& astNodeId_,
    const std::int32_t referenceId_) override
  {
    return _service -> getReferenceCount(astNodeId_, referenceId_);
  }

  void getReferences(
    std::vector<language::AstNodeInfo>& return_,
    const core::AstNodeId& astNodeId_,
    const std::int32_t referenceId_,
    const std::vector<std::string>& tags_) override
  {
    _service -> getReferences(return_, astNodeId_, referenceId_, tags_);
  }

  void getFileReferenceTypes(
    std::map<std::string, std::int32_t>& return_) override
  {
    _service -> getFileReferenceTypes(return_);
  }

  std::int32_t getFileReferenceCount(
    const core::FileId& fileId_,
    const std::int32_t referenceId_) override
  {
    return _service -> getFileReferenceCount(fileId_, referenceId_);
  }

  void getFileReferences(
    std::vector<language::AstNodeInfo>& return_,
    const core::FileId& fileId_,
    const std::int32_t referenceId_) override
  {
    _service -> getFileReferences(return_, fileId_, referenceId_);
  }

  void getDiagramTypes(
    std::map<std::string, std::int32_t>& return_,
    const core::AstNodeId& astNodeId_
    ) override
  {
    _service -> getDiagramTypes(return_, astNodeId_);
  }

  void getDiagram(
    std::string& return_,
    const core::AstNodeId& astNodeId_,
    const std::int32_t diagramId_) override
  {
    _service -> getDiagram(return_, astNodeId_, diagramId_);
  }

  void getSyntaxHighlight(
  std::vector<language::SyntaxHighlight>& return_,
  const core::FileRange& range_,
  const std::vector<std::string>& content_) override
  {
    _service -> getSyntaxHighlight(return_, range_, content_);
  }

private:
  /**
   * Service interface for IPC communication.
   */
  std::unique_ptr<JavaServiceIf> _service;

  /**
   * Object to store Thrift messages during connecting to the Java server
   */
  static std::stringstream thrift_ss;
};

} // java

namespace language
{

namespace fs = boost::filesystem;
namespace pr = boost::process;
namespace pt = boost::property_tree;
using TransportException = apache::thrift::transport::TTransportException;

class JavaServiceHandler : virtual public LanguageServiceIf
{
public:

  JavaServiceHandler(
    std::shared_ptr<odb::database> db_,
    std::shared_ptr<std::string> datadir_,
    const cc::webserver::ServerContext& context_);

  std::string getRawDbContext();

  void getFileTypes(std::vector<std::string>& return_) override;

  void getAstNodeInfo(
    AstNodeInfo& return_,
    const core::AstNodeId& astNodeId_) override;

  void getAstNodeInfoByPosition(
    AstNodeInfo& return_,
    const core::FilePosition& fpos_) override;

  void getSourceText(
    std::string& return_,
    const core::AstNodeId& astNodeId_) override;

  void getDocumentation(
    std::string& return_,
    const core::AstNodeId& astNodeId_) override;

  void getProperties(
    std::map<std::string, std::string>& return_,
    const core::AstNodeId& astNodeId_) override;

  void getDiagramTypes(
    std::map<std::string, std::int32_t>& return_,
    const core::AstNodeId& astNodeId_) override;

  void getDiagram(
    std::string& return_,
    const core::AstNodeId& astNodeId_,
    const std::int32_t diagramId_) override;

  void getDiagramLegend(
    std::string& return_,
    const std::int32_t diagramId_) override;

  void getFileDiagramTypes(
    std::map<std::string, std::int32_t>& return_,
    const core::FileId& fileId_) override;

  void getFileDiagram(
    std::string& return_,
    const core::FileId& fileId_,
    const int32_t diagramId_) override;

  void getFileDiagramLegend(
    std::string& return_,
    const std::int32_t diagramId_) override;

  void getReferenceTypes(
    std::map<std::string, std::int32_t>& return_,
    const core::AstNodeId& astNodeId) override;

  std::int32_t getReferenceCount(
    const core::AstNodeId& astNodeId_,
    const std::int32_t referenceId_) override;

  void getReferences(
    std::vector<AstNodeInfo>& return_,
    const core::AstNodeId& astNodeId_,
    const std::int32_t referenceId_,
    const std::vector<std::string>& tags_) override;

  void getReferencesInFile(
    std::vector<AstNodeInfo>& return_,
    const core::AstNodeId& astNodeId_,
    const std::int32_t referenceId_,
    const core::FileId& fileId_,
    const std::vector<std::string>& tags_) override;

  void getReferencesPage(
    std::vector<AstNodeInfo>& return_,
    const core::AstNodeId& astNodeId_,
    const std::int32_t referenceId_,
    const std::int32_t pageSize_,
    const std::int32_t pageNo_) override;

  void getFileReferenceTypes(
    std::map<std::string, std::int32_t>& return_,
    const core::FileId& fileId_) override;

  std::int32_t getFileReferenceCount(
    const core::FileId& fileId_,
    const std::int32_t referenceId_) override;

  void getFileReferences(
    std::vector<AstNodeInfo>& return_,
    const core::FileId& fileId_,
    const std::int32_t referenceId_) override;

  void getSyntaxHighlight(
    std::vector<SyntaxHighlight>& return_,
    const core::FileRange& range_) override;

private:
  std::shared_ptr<odb::database> _db;
  util::OdbTransaction _transaction;

  std::shared_ptr<std::string> _datadir;
  const cc::webserver::ServerContext& _context;

  fs::path _java_path;
  pr::child c;
  cc::service::java::JavaQueryHandler javaQueryHandler;
};

} // language
} // service
} // cc

#endif // CC_SERVICE_JAVA_JAVASERVICE_H
