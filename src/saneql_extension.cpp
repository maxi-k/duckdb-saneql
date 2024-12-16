#define DUCKDB_EXTENSION_MAIN
//---------------------------------------------------------------------------
#include "saneql_extension.hpp"
//---------------------------------------------------------------------------
// duckdb
#include <duckdb.hpp>
#include <duckdb/common/exception.hpp>
#include <duckdb/common/string_util.hpp>
#include <duckdb/parser/parser.hpp>
#include <duckdb/parser/parser_extension.hpp>
#include <duckdb/parser/statement/extension_statement.hpp>
#include <duckdb/parser/tableref/basetableref.hpp>
#include <duckdb/planner/binder.hpp>
//---------------------------------------------------------------------------
// saneql
#include "compiler/SaneQLCompiler.hpp"
#include "infra/Schema.hpp"
#include "parser/ASTBase.hpp"
#include "parser/SaneQLParser.hpp"
#include "sql/SQLWriter.hpp"
//---------------------------------------------------------------------------
// std
#include <map>
#include <string_view>
//---------------------------------------------------------------------------
using namespace saneql;
//---------------------------------------------------------------------------
static std::ofstream &log() {
  static std::ofstream log_file("/tmp/duckdb-saneql.log");
  return log_file;
}
//---------------------------------------------------------------------------
namespace duckdb {
using std::is_enum_v;

//---------------------------------------------------------------------------
/// main entrypoint
void load_saneql(DatabaseInstance &db) {
  log() << "start loading saneql extension for duckdb " << DUCKDB_MAJOR_VERSION
        << "." << DUCKDB_MINOR_VERSION << "." << DUCKDB_PATCH_VERSION
        << std::endl;
  auto &config = DBConfig::GetConfig(db);
  SaneqlParserExtension saneql_parser;
  config.parser_extensions.push_back(saneql_parser);
  config.operator_extensions.push_back(make_uniq<SaneqlOperatorExtension>());
  log() << "loaded saneql extension" << std::endl;
}
//---------------------------------------------------------------------------
void SaneqlExtension::Load(DuckDB &db) { load_saneql(*db.instance); }
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
/// parser extension
//---------------------------------------------------------------------------
class SaneqlParserExtensionInfo : public ParserExtensionInfo {
  unique_ptr<SaneqlParseData> stashed_query;

public:
  void stash_compiled_query(SaneqlParseData *query) {
    D_ASSERT(!stashed_query);
    stashed_query.reset(query);
  }

  unique_ptr<SaneqlParseData> pop_stashed_query() {
    D_ASSERT(stashed_query);
    return std::move(stashed_query);
  }
};
//---------------------------------------------------------------------------
SaneqlParserExtension::SaneqlParserExtension() : ParserExtension() {
  parse_function = saneql_parse;
  plan_function = saneql_plan;
  parser_info = make_shared_ptr<SaneqlParserExtensionInfo>();
}
//---------------------------------------------------------------------------
class SaneAST {
  using AST = saneql::ast::AST;
  std::string query; // ! container contains string_view to this
  saneql::ASTContainer container;
  saneql::ast::AST *parsed;

public:
  SaneAST(const std::string &sql) : query(sql) {
    auto view = std::string_view(
        query.data(), query.back() == ';' ? query.size() - 1 : query.size());
    parsed = SaneQLParser::parse(container, view);
  };

public:
  AST &get() { return *parsed; }
  std::string_view getQuery() const { return query; }
};
//---------------------------------------------------------------------------
unique_ptr<ParserExtensionParseData> SaneqlParseData::Copy() const {
  log() << "copying parse data " << std::endl;
  auto res = make_uniq<SaneqlParseData>(ast);
  log() << "result ast: " << res->ast.get() << std::endl;
  return res;
}
//---------------------------------------------------------------------------
string SaneqlParseData::ToString() const {
  log() << "stringifying parse data " << std::endl;
  std::stringstream result;
  result << "(SaneqlParseData :ast (" << (this->ast)->getQuery() << "))";
  return result.str();
}
//---------------------------------------------------------------------------
/// parse saneql text to an ast
ParserExtensionParseResult
SaneqlParserExtension::saneql_parse(ParserExtensionInfo *,
                                    const std::string &query) {
  using Res = ParserExtensionParseResult;
  try {
    auto ast = make_shared_ptr<SaneAST>(query);
    return Res(make_uniq<SaneqlParseData>(ast));
  } catch (std::exception &e) {
    log() << "saneql_parse error " << e.what() << std::endl;
    return Res(e.what());
  }
};
//---------------------------------------------------------------------------
/// a saneql schema implementation for duckdb
class DuckDBSchema final : public saneql::Schema {
  ClientContext &ctx;
  Binder &query_binder;
  shared_ptr<Binder> private_binder;
  mutable std::map<std::string, Table> binding_cache;

public:
  DuckDBSchema(ClientContext &ctx, Binder &parent_binder)
      : ctx(ctx), query_binder(parent_binder),
        private_binder(Binder::CreateBinder(ctx, query_binder)) {}
  const Table *lookupTable(const std::string &name) const override {
    return lookup_using_binder(name);
    // return lookup_table_name(name, true);
  }

private:
  Table *lookup_using_binder(const std::string &name) const {
    std::string err;
    auto existing = binding_cache.find(name);
    if (existing != binding_cache.end()) {
      log() << "found cached table " << name << std::endl;
      return &existing->second;
    }
    auto table_ref = make_uniq<BaseTableRef>();
    table_ref->table_name = name;
    log() << "trying to bind table " << name << std::endl;
    try {
      private_binder->Bind(*table_ref->Copy());
    } catch (CatalogException &e) {
      log() << "didn't find table " << name << " in catalog: " << e.what()
            << std::endl;
    }
    log() << "trying to get bound table " << std::endl;
    if (auto tbl = get_bound_table(ctx, *private_binder, name)) {
      return &binding_cache.emplace_hint(existing, name, *tbl)->second;
    }
    return nullptr;
  }

  static std::optional<Table>
  get_bound_table(ClientContext &ctx, Binder &binder, const std::string &name) {
    // binder.bind_context.GetTypesAndNames(vector<string> &result_names,
    // vector<LogicalType> &result_types)
    Table result;
    auto &bc = binder.bind_context;
    ErrorData error;
    if (auto t = bc.GetBinding(name, error)) {
      for (auto i = 0; i != t->types.size(); ++i) {
        log() << "  bound column " << name << "." << t->names[i] << " of type "
              << t->types[i].ToString() << std::endl;
        result.columns.emplace_back(StringUtil::Lower(t->names[i]),
                                    duckToSaneType(t->types[i]));
      }
      return {result};
    } else {
      return std::nullopt;
    }
  }

  static Type duckToSaneType(LogicalType type) {
    using enum LogicalTypeId;
    if (type.IsIntegral()) {
      return Type::getInteger();
    }
    if (type.IsNumeric()) {
      uint8_t width, scale;
      type.GetDecimalProperties(width, scale);
      return Type::getDecimal(width, scale);
    }
    if (LogicalType::TypeIsTimestamp(type)) {
      return Type::getDate(); // TODO saneql timestamp type
    }
    switch (type.id()) {
    case BOOLEAN:
      return Type::getBool();
    case DATE:
    case TIME:
    case TIME_TZ:
      return Type::getDate(); // TODO saneql timestamp type
    case CHAR:
    case VARCHAR:
      return Type::getText();
      return Type::getText(); // duckdb doesn't track varchar maxlen internally
    case INTERVAL:
      return Type::getInterval();
    default:
      log() << "can't map duckdb type '" << type.ToString()
            << "' to saneql, using unknown" << std::endl;
      return Type::getUnknown();
    }
  }
};
//---------------------------------------------------------------------------
/// convert a saneql ast to a duckdb logical plan
ParserExtensionPlanResult SaneqlParserExtension::saneql_plan(
    ParserExtensionInfo *info, ClientContext &,
    unique_ptr<ParserExtensionParseData> parse_data) {
  // stash away the ast for the operator extension
  static_cast<SaneqlParserExtensionInfo *>(info)->stash_compiled_query(
      static_cast<SaneqlParseData *>(parse_data.release()));
  throw BinderException("using compiled sql instead of implementing custom "
                        "table functions; continue with saneql_bind");
};
//---------------------------------------------------------------------------
/// operator extension
//---------------------------------------------------------------------------
BoundStatement SaneqlOperatorExtension::saneql_bind(ClientContext &client,
                                                    Binder &parent_binder,
                                                    OperatorExtensionInfo *,
                                                    SQLStatement &statement) {
  log() << "calling saneql_bind" << std::endl;
  if (statement.type != StatementType::EXTENSION_STATEMENT) {
    return {};
  }
  auto &saneql_data = static_cast<ExtensionStatement &>(statement);
  // duckdb loops over all loaed extensions; return an invalid result if our
  // parser didn't create the statement passed to us
  if (saneql_data.extension.parse_function !=
      SaneqlParserExtension::saneql_parse) {
    return {};
  }
  auto parse_data = static_cast<SaneqlParserExtensionInfo *>(
                        saneql_data.extension.parser_info.get())
                        ->pop_stashed_query();

  // parse the saneql ast to actual sql using the binding info from the client
  // context
  DuckDBSchema schema(client, parent_binder);

  // XXX at this point we're translating the AST to SQL instead of building
  // a 'proper' representation of a duckdb operator tree.
  std::string sql_string;
  try {
    SQLWriter writer;
    SaneQLCompiler compiler(schema, writer);
    log() << "compiling saneql: " << &parse_data->ast->get() << std::endl;
    sql_string = compiler.compile(&parse_data->ast->get());
    log() << "compiled sql: " << sql_string << std::endl;
  } catch (std::exception &e) {
    log() << "error while compiling saneql to SQL: " << e.what() << std::endl;
    throw duckdb::ParserException(e.what());
  }

  vector<unique_ptr<SQLStatement>> statements;
  try {
    Parser parser(client.GetParserOptions());
    parser.ParseQuery(sql_string);
    statements = std::move(parser.statements);
  } catch (std::exception &e) {
    log() << "error while parsing SQL during saneql_plan: " << e.what()
          << std::endl;
    throw e;
  }
  if (statements.size() > 1) {
    log() << "warning: multiple SQL statements resulted from saneql; only "
             "using first. compiled sql: "
          << sql_string << std::endl;
  }

  auto binder = Binder::CreateBinder(client, &parent_binder);
  return binder->Bind(*statements[0]);
}
//---------------------------------------------------------------------------

} // namespace duckdb
//---------------------------------------------------------------------------
/// C API extension loading code
extern "C" {
//---------------------------------------------------------------------------
DUCKDB_EXTENSION_API void saneql_init(duckdb::DatabaseInstance &db) {
  load_saneql(db);
}
//---------------------------------------------------------------------------
DUCKDB_EXTENSION_API const char *saneql_version() {
  return duckdb::DuckDB::LibraryVersion();
}
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
