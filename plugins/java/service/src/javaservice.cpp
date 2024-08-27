#include <util/util.h>
#include <util/dbutil.h>

#include <model/file.h>
#include <model/file-odb.hxx>

#include <service/javaservice.h>

namespace
{

namespace model = cc::model;

typedef odb::query<model::File> FileQuery;

}

namespace cc
{
namespace service
{
namespace language
{

JavaServiceHandler::JavaServiceHandler(
  std::shared_ptr<odb::database> db_,
  std::shared_ptr<std::string> datadir_,
  const cc::webserver::ServerContext& context_)
  : _db(db_),
    _transaction(db_),
    _datadir(datadir_),
    _context(context_)
{
  _java_path = pr::search_path("java");
  std::string raw_db_context = getRawDbContext();

  std::vector<std::string> _java_args{
    "-DrawDbContext=" + raw_db_context,
    "-jar",
    "../lib/java/javaservice.jar"
  };
  c = pr::child(_java_path, _java_args, pr::std_out > stdout);

  try {
    javaQueryHandler.getClientInterface(25000);
  } catch (TransportException& ex) {
    LOG(error) << "[javaservice] Starting service failed!";
  }
}

std::string JavaServiceHandler::getRawDbContext() {
  pt::ptree _pt;
  pt::read_json(*_datadir + "/project_info.json", _pt);

  return _pt.get<std::string>("database");
}

void JavaServiceHandler::getFileTypes(std::vector<std::string>& return_)
{
  return_.push_back("JAVA");
  return_.push_back("Dir");
}

void JavaServiceHandler::getAstNodeInfo(
  AstNodeInfo& return_,
  const core::AstNodeId& astNodeId_)
{
  javaQueryHandler.getAstNodeInfo(return_, astNodeId_);
}

void JavaServiceHandler::getAstNodeInfoByPosition(
  AstNodeInfo& return_,
  const core::FilePosition& fpos_)
{
  javaQueryHandler.getAstNodeInfoByPosition(return_, fpos_);
}

void JavaServiceHandler::getSourceText(
  std::string& return_,
  const core::AstNodeId& astNodeId_)
{
  core::FileRange fileRange;

  javaQueryHandler.getFileRange(fileRange, astNodeId_);

  return_ = _transaction([&, this](){
    model::FilePtr file = _db->query_one<model::File>(
      FileQuery::id == std::stoull(fileRange.file));

    if (!file) {
      return std::string();
    }

    return cc::util::textRange(
      file->content.load()->content,
      fileRange.range.startpos.line,
      fileRange.range.startpos.column,
      fileRange.range.endpos.line,
      fileRange.range.endpos.column);
  });
}

void JavaServiceHandler::getProperties(
  std::map<std::string, std::string>& return_,
  const core::AstNodeId& astNodeId_)
{
  javaQueryHandler.getProperties(return_, astNodeId_);
}

void JavaServiceHandler::getDocumentation(
  std::string& return_,
  const core::AstNodeId& astNodeId_)
{
  javaQueryHandler.getDocumentation(return_, astNodeId_);
}

void JavaServiceHandler::getDiagramTypes(
  std::map<std::string, std::int32_t>& return_,
  const core::AstNodeId& astNodeId_)
{
  javaQueryHandler.getDiagramTypes(return_, astNodeId_);
}

void JavaServiceHandler::getDiagram(
  std::string& return_,
  const core::AstNodeId& astNodeId_,
  const std::int32_t diagramId_)
{
  javaQueryHandler.getDiagram(return_, astNodeId_, diagramId_);
}

void JavaServiceHandler::getDiagramLegend(
  std::string& return_,
  const std::int32_t diagramId_)
{
  LOG(info) << "getDiagramLegend";
}

void JavaServiceHandler::getFileDiagramTypes(
  std::map<std::string, std::int32_t>& return_,
  const core::FileId& fileId_)
{
  LOG(info) << "getFileDiagramTypes";
}

void JavaServiceHandler::getFileDiagram(
  std::string& return_,
  const core::FileId& fileId_,
  const int32_t diagramId_)
{
  LOG(info) << "getFileDiagram";
}

void JavaServiceHandler::getFileDiagramLegend(
  std::string& return_,
  const std::int32_t diagramId_)
{
  LOG(info) << "getFileDiagramLegend";
}

void JavaServiceHandler::getReferenceTypes(
  std::map<std::string, std::int32_t>& return_,
  const core::AstNodeId& astNodeId_)
{
  javaQueryHandler.getReferenceTypes(return_, astNodeId_);
}

std::int32_t JavaServiceHandler::getReferenceCount(
  const core::AstNodeId& astNodeId_,
  const std::int32_t referenceId_)
{
  return javaQueryHandler.getReferenceCount(astNodeId_, referenceId_);
}

void JavaServiceHandler::getReferences(
  std::vector<AstNodeInfo>& return_,
  const core::AstNodeId& astNodeId_,
  const std::int32_t referenceId_,
  const std::vector<std::string>& tags_)
{
  javaQueryHandler.getReferences(return_, astNodeId_, referenceId_, tags_);
}

void JavaServiceHandler::getReferencesInFile(
  std::vector<AstNodeInfo>& /* return_ */,
  const core::AstNodeId& /* astNodeId_ */,
  const std::int32_t /* referenceId_ */,
  const core::FileId& /* fileId_ */,
  const std::vector<std::string>& /* tags_ */)
{
  // TODO
}

void JavaServiceHandler::getReferencesPage(
  std::vector<AstNodeInfo>& /* return_ */,
  const core::AstNodeId& /* astNodeId_ */,
  const std::int32_t /* referenceId_ */,
  const std::int32_t /* pageSize_ */,
  const std::int32_t /* pageNo_ */)
{
  // TODO
}

void JavaServiceHandler::getFileReferenceTypes(
  std::map<std::string, std::int32_t>& return_,
  const core::FileId& /* fileId_*/)
{
  javaQueryHandler.getFileReferenceTypes(return_);
}

std::int32_t JavaServiceHandler::getFileReferenceCount(
  const core::FileId& fileId_,
  const std::int32_t referenceId_)
{
  return javaQueryHandler.getFileReferenceCount(fileId_, referenceId_);
}

void JavaServiceHandler::getFileReferences(
  std::vector<AstNodeInfo>& return_,
  const core::FileId& fileId_,
  const std::int32_t referenceId_)
{
  javaQueryHandler.getFileReferences(return_, fileId_, referenceId_);
}

void JavaServiceHandler::getSyntaxHighlight(
  std::vector<SyntaxHighlight>& return_,
  const core::FileRange& range_)
{
  /*
  std::vector<std::string> content;

  _transaction([&, this]() {

      //--- Load the file content and break it into lines ---//

      model::FilePtr file = _db->query_one<model::File>(
        FileQuery::id == std::stoull(range_.file));

      if (!file || !file->content.load())
        return;

      std::istringstream s(file->content->content);
      std::string line;
      while (std::getline(s, line))
        content.push_back(line);
  });

  javaQueryHandler.getSyntaxHighlight(return_, range_, content);
  */
}

} // language

namespace java
{

// Initialize static member
std::stringstream JavaQueryHandler::thrift_ss;

} // java
} // service
} // cc
