#pragma once
//---------------------------------------------------------------------------
#include "duckdb.hpp"
#include "duckdb/parser/statement/extension_statement.hpp"
#include <fstream>
//---------------------------------------------------------------------------
namespace duckdb {
//---------------------------------------------------------------------------
class SaneqlExtension : public Extension {
   public:
   void Load(DuckDB& db) override;
   std::string Name() override { return name(); }
   static std::string name() { return "saneql"; }
};
//---------------------------------------------------------------------------
struct SaneAST;
struct SaneqlParseData : public ParserExtensionParseData {
   shared_ptr<SaneAST> ast;
   explicit SaneqlParseData(shared_ptr<SaneAST> ast) : ast(ast) {}

   unique_ptr<ParserExtensionParseData> Copy() const override;
};
//---------------------------------------------------------------------------
struct SaneqlParserExtension : public ParserExtension {
   SaneqlParserExtension();

   /// parses saneql into an AST.
   static ParserExtensionParseResult saneql_parse(ParserExtensionInfo*, const std::string& query);
   // annoyingly, we can only return a 'table function' here, so we have to
   // implement an operator extension (that gets called upon when the plan
   // function throws). There, we can return a logical operator from the parsed
   // SQL without having to implement a table function.
   static ParserExtensionPlanResult saneql_plan(ParserExtensionInfo*, ClientContext&, unique_ptr<ParserExtensionParseData>);
};
//---------------------------------------------------------------------------
struct SaneqlOperatorExtension : public OperatorExtension {
   SaneqlOperatorExtension() : OperatorExtension() { Bind = saneql_bind; }
   ~SaneqlOperatorExtension() = default;

   std::string GetName() override { return SaneqlExtension::name(); }

// #if DUCKDB_MINOR_VERSION <= 8 && DUCKDB_MAJOR_VERSION == 0
//    unique_ptr<LogicalExtensionOperator> Deserialize(LogicalDeserializationState&, FieldReader&) override {
//       throw std::runtime_error("saneql operator deserialization not implemented");
//    };
// #else
   unique_ptr<LogicalExtensionOperator> Deserialize(Deserializer &deserializer) override {
      throw std::runtime_error("saneql operator deserialization not implemented");
   };
// #endif

//  unique_ptr<LogicalExtensionOperator> FormatDeserialize(FormatDeserializer &deserializer) {
//     throw std::runtime_error("saneql operator deserialization not implemented");
//  }

   private:
   static BoundStatement saneql_bind(ClientContext& context, Binder& binder, OperatorExtensionInfo* info, SQLStatement& statement);
};
//---------------------------------------------------------------------------
} // namespace duckdb
//---------------------------------------------------------------------------
