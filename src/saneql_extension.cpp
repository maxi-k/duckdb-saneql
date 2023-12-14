#define DUCKDB_EXTENSION_MAIN
//---------------------------------------------------------------------------
#include "saneql_extension.hpp"
//---------------------------------------------------------------------------
// duckdb
#include <duckdb.hpp>
#include <duckdb/common/exception.hpp>
#include <duckdb/common/string_util.hpp>
#include <duckdb/parser/parser.hpp>
#include <duckdb/parser/statement/extension_statement.hpp>
#include <duckdb/parser/parser_extension.hpp>
#include <duckdb/planner/binder.hpp>
#include <duckdb/catalog/catalog.hpp>
#include <duckdb/catalog/catalog_entry/table_catalog_entry.hpp>
#include <duckdb/catalog/catalog_entry/view_catalog_entry.hpp>
//---------------------------------------------------------------------------
// saneql
#include "compiler/SaneQLCompiler.hpp"
#include "parser/ASTBase.hpp"
#include "parser/SaneQLParser.hpp"
#include "infra/Schema.hpp"
#include "sql/SQLWriter.hpp"
//---------------------------------------------------------------------------
// std
#include <stdexcept>
#include <map>
#include <string_view>
//---------------------------------------------------------------------------
using namespace saneql;
//---------------------------------------------------------------------------
static std::ofstream& log() {
   static std::ofstream log_file("/tmp/duckdb-saneql.log");
   return log_file;
}
//---------------------------------------------------------------------------
namespace duckdb {
using std::is_enum_v;

//---------------------------------------------------------------------------
/// main entrypoint
void load_saneql(DatabaseInstance& db) {
   log() << "start loading saneql extension for duckdb " <<
      DUCKDB_MAJOR_VERSION << "." << DUCKDB_MINOR_VERSION << "." << DUCKDB_PATCH_VERSION << std::endl;
   auto& config = DBConfig::GetConfig(db);
   SaneqlParserExtension saneql_parser;
   config.parser_extensions.push_back(saneql_parser);
   config.operator_extensions.push_back(std::make_unique<SaneqlOperatorExtension>());
   log() << "loaded saneql extension" << std::endl;
}
//---------------------------------------------------------------------------
void SaneqlExtension::Load(DuckDB& db) { load_saneql(*db.instance); }
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
/// parser extension
//---------------------------------------------------------------------------
class SaneqlParserExtensionInfo : public ParserExtensionInfo {
   unique_ptr<SaneqlParseData> stashed_query;

   public:
   void stash_compiled_query(SaneqlParseData* query) {
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
   parser_info = std::make_shared<SaneqlParserExtensionInfo>();
}
//---------------------------------------------------------------------------
class SaneAST {
   using AST = saneql::ast::AST;
   std::string query; // ! container contains string_view to this
   saneql::ASTContainer container;
   saneql::ast::AST* parsed;

   public:
   SaneAST(const std::string& sql) : query(sql) {
      auto view = std::string_view(query.data(), query.back() == ';' ? query.size() - 1 : query.size());
      parsed = SaneQLParser::parse(container, view);
   };

   public:
   AST& get() { return *parsed; }
};
//---------------------------------------------------------------------------
unique_ptr<ParserExtensionParseData> SaneqlParseData::Copy() const {
   log() << "copying parse data " << std::endl;
   auto res = std::make_unique<SaneqlParseData>(ast);
   log() << "result ast: " << res->ast.get() << std::endl;
   return res;
}
//---------------------------------------------------------------------------
/// parse saneql text to an ast
ParserExtensionParseResult SaneqlParserExtension::saneql_parse(ParserExtensionInfo*, const std::string& query) {
   try {
      auto ast = std::make_shared<SaneAST>(query);
      return {std::make_unique<SaneqlParseData>(ast)};
   } catch (std::exception& e) {
      log() << "saneql_parse error " << e.what() << std::endl;
      return {e.what()};
   }
};
//---------------------------------------------------------------------------
/// a saneql schema implementation for duckdb
class DuckDBSchema final : public saneql::Schema {
   ClientContext& ctx;
   mutable std::map<std::string, Table> binding_cache;

   public:
   DuckDBSchema(ClientContext& ctx) : ctx(ctx) {}
   const Table* lookupTable(const std::string& name) const override {
      std::string err;
      auto existing = binding_cache.find(name);
      if (existing != binding_cache.end()) {
         return &existing->second;
      }
      // TODO probably non exhaustive
      if (auto found = get_table(ctx, name)) {
         Table result;
         if (auto table = dynamic_cast<TableCatalogEntry*>(found)) {
            auto& it = table->GetColumns();
            result.columns.reserve(it.LogicalColumnCount());
            for (auto& coldef : it.Logical()) {
               result.columns.emplace_back(StringUtil::Lower(coldef.GetName()), duckToSaneType(coldef.GetType()));
            }
         } else if (auto view = dynamic_cast<ViewCatalogEntry*>(found)) {
            result.columns.reserve(view->aliases.size());
            for (auto i = 0u; i != view->aliases.size(); ++i) {
               result.columns.emplace_back(StringUtil::Lower(view->aliases[i]), duckToSaneType(view->types[i]));
            }
         } else {
            log() << "unknown catalog entry " << typeid(found).name() << std::endl;
            return nullptr;
         }
         return &binding_cache.emplace_hint(existing, name, result)->second;
      }
      return nullptr;
   }

   private:

   static CatalogEntry* get_table(ClientContext& ctx, const std::string& name) {
      // interface after duckdb 0.8.0
#if __has_include(<duckdb/common/enums/on_entry_not_found.hpp>) || __has_include("duckdb/common/enums/on_entry_not_found.hpp")
      auto on_not_found = OnEntryNotFound::RETURN_NULL;
      auto opt_ptr = Catalog::GetEntry(ctx, CatalogType::TABLE_ENTRY, INVALID_CATALOG, INVALID_SCHEMA, name, on_not_found);
      return opt_ptr ? opt_ptr.get() : nullptr;
#else
      auto on_not_found = true; // return null
      return Catalog::GetEntry(ctx, CatalogType::TABLE_ENTRY, INVALID_CATALOG, INVALID_SCHEMA, name, on_not_found);
#endif
   }

   static Type duckToSaneType(LogicalType type) {
      using enum LogicalTypeId;
      if (type.IsIntegral()) { return Type::getInteger(); }
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
ParserExtensionPlanResult SaneqlParserExtension::saneql_plan(ParserExtensionInfo* info, ClientContext&, unique_ptr<ParserExtensionParseData> parse_data) {
   // stash away the ast for the operator extension
   static_cast<SaneqlParserExtensionInfo*>(info)->stash_compiled_query(static_cast<SaneqlParseData*>(parse_data.release()));
   throw BinderException("using compiled sql instead of implementing custom table functions; continue with saneql_bind");
};
//---------------------------------------------------------------------------
/// operator extension
//---------------------------------------------------------------------------
BoundStatement SaneqlOperatorExtension::saneql_bind(ClientContext& client, Binder& parent_binder, OperatorExtensionInfo*, SQLStatement& statement) {
   log() << "calling saneql_bind" << std::endl;
   if (statement.type != StatementType::EXTENSION_STATEMENT) { return {}; }
   auto& saneql_data = static_cast<ExtensionStatement&>(statement);
   // duckdb loops over all loaed extensions; return an invalid result if our
   // parser didn't create the statement passed to us
   if (saneql_data.extension.parse_function != SaneqlParserExtension::saneql_parse) { return {}; }
   auto parse_data = static_cast<SaneqlParserExtensionInfo*>(saneql_data.extension.parser_info.get())->pop_stashed_query();

   // parse the saneql ast to actual sql using the binding info from the client context
   DuckDBSchema schema(client);

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
   } catch (std::exception& e) {
      log() << "error while parsing SQL during saneql_plan: " << e.what() << std::endl;
      throw e;
   }
   if (statements.size() > 1) {
      log() << "warning: multiple SQL statements resulted from saneql; only using first. compiled sql: " << sql_string << std::endl;
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
DUCKDB_EXTENSION_API void duckdb_saneql_init(duckdb::DatabaseInstance& db) { load_saneql(db); }
//---------------------------------------------------------------------------
DUCKDB_EXTENSION_API const char* duckdb_saneql_version() {return duckdb::DuckDB::LibraryVersion();}
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
