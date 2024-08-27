package service.srcjava;

import cc.service.core.*;
import cc.service.java.JavaService;
import cc.service.language.AstNodeInfo;
import cc.service.language.SyntaxHighlight;
import model.*;
import model.enums.MemberTypeKind;
import model.enums.RelationKind;
import org.apache.thrift.TException;
import service.srcjava.enums.DiagramType;
import service.srcjava.enums.FileReferenceType;
import service.srcjava.enums.ReferenceType;

import java.util.*;
import java.util.stream.Collectors;

import static service.srcjava.JavaQueryFactory.*;

public class JavaQueryHandler implements JavaService.Iface {
  @Override
  public FileRange getFileRange(String javaAstNodeId) {
    JavaAstNode javaAstNode = queryJavaAstNode(Long.parseLong(javaAstNodeId));

    return getFileRange(javaAstNode);
  }

  @Override
  public AstNodeInfo getAstNodeInfo(String javaAstNodeId) {
    JavaAstNode javaAstNode = queryJavaAstNode(Long.parseLong(javaAstNodeId));

    return createAstNodeInfo(javaAstNode);
  }

  @Override
  public AstNodeInfo getAstNodeInfoByPosition(FilePosition fpos)
    throws TException
  {
    List<JavaAstNode> javaAstNodes = queryJavaAstNodeByPosition(fpos);

    if (javaAstNodes.isEmpty()) {
      InvalidPos ex = new InvalidPos();
      ex.msg = "There are no any JavaAstNode at this position.";
      ex.fpos = fpos;

      throw ex;
    }

    JavaAstNode minJavaAstNode = javaAstNodes.get(0);

    for (JavaAstNode javaAstNode : javaAstNodes) {
      if (javaAstNode.isVisibleInSourceCode() &&
        javaAstNode.isRangeSmaller(minJavaAstNode)) {
        minJavaAstNode = javaAstNode;
      }
    }

    return createAstNodeInfo(
      minJavaAstNode, getTags(Collections.singletonList(minJavaAstNode))
    );
  }

  @Override
  public Map<String, String> getProperties(String javaAstNodeId) {
    Map<String, String> properties = new HashMap<>();
    long javaAstNodeIdLong = Long.parseLong(javaAstNodeId);
    JavaAstNode javaAstNode = queryJavaAstNode(javaAstNodeIdLong);

    switch (javaAstNode.getSymbolType()){
      case VARIABLE: {
        List<JavaVariable> javaVariables = queryJavaVariables(javaAstNode);

        if (!javaVariables.isEmpty()) {
          JavaVariable javaVariable = javaVariables.get(0);

          properties.put("Final variable",
            Boolean.toString(javaVariable.isFinal()));
          properties.put("Static variable",
            Boolean.toString(javaVariable.isStatic()));
          properties.put("Name", javaVariable.getName());
          properties.put("Qualified name", javaVariable.getQualifiedName());
          properties.put("Type", javaVariable.getQualifiedType());

          return properties;
        }
        break;
      }
      case CONSTRUCTOR: {
        List<JavaConstructor> javaConstructors =
          queryJavaConstructors(javaAstNode);

        if (!javaConstructors.isEmpty()) {
          JavaConstructor javaConstructor = javaConstructors.get(0);

          properties.put("Name", javaConstructor.getName());
          properties.put("Qualified name", javaConstructor.getQualifiedName());

          return properties;
        }
        break;
      }
      case METHOD: {
        List<JavaMethod> javaMethods = queryJavaMethods(javaAstNode);

        if (!javaMethods.isEmpty()) {
          JavaMethod javaMethod = javaMethods.get(0);

          properties.put("Final method",
            Boolean.toString(javaMethod.isFinal()));
          properties.put("Static method",
            Boolean.toString(javaMethod.isStatic()));
          properties.put("Name", javaMethod.getName());
          properties.put("Qualified name", javaMethod.getQualifiedName());
          properties.put("Type", javaMethod.getQualifiedType());

          return properties;
        }
        break;
      }
      case TYPE: {
        List<JavaRecord> javaRecords = queryJavaRecords(javaAstNode);

        if (!javaRecords.isEmpty()) {
          JavaRecord javaRecord = javaRecords.get(0);

          properties.put("Abstract type",
            Boolean.toString(javaRecord.isAbstract()));
          properties.put("Final type",
            Boolean.toString(javaRecord.isFinal()));
          properties.put("Static type",
            Boolean.toString(javaRecord.isStatic()));
          properties.put("Name", javaRecord.getName());
          properties.put("Qualified name", javaRecord.getQualifiedName());

          return properties;
        }
        break;
      }
      case ENUM: {
        List<JavaEnum> javaEnums = queryJavaEnums(javaAstNode);

        if (!javaEnums.isEmpty()) {
          JavaEnum javaEnum = javaEnums.get(0);

          properties.put("Name", javaEnum.getName());
          properties.put("Qualified name", javaEnum.getQualifiedName());
        }
        break;
      }
      case ENUM_CONSTANT: {
        List<JavaEnumConstant> javaEnumConstants =
          queryJavaEnumConstants(javaAstNode);

        if (!javaEnumConstants.isEmpty()) {
          JavaEnumConstant javaEnumConstant = javaEnumConstants.get(0);

          properties.put("Name", javaEnumConstant.getName());
          properties.put("Qualified name", javaEnumConstant.getQualifiedName());
          properties.put(
            "Value", Integer.toString(javaEnumConstant.getValue())
          );

          return properties;
        }
        break;
      }
      case INITIALIZER: {
        List<JavaInitializer> javaInitializers =
          queryJavaInitializers(javaAstNode);

        if (!javaInitializers.isEmpty()) {
          JavaInitializer javaInitializer = javaInitializers.get(0);

          properties.put("Kind", javaInitializer.getKind().getName());
        }
        break;
      }
    }

    return properties;
  }

  @Override
  public String getDocumentation(String javaAstNodeId) {
    StringBuilder sb = new StringBuilder();

    long javaAstNodeIdLong = Long.parseLong(javaAstNodeId);
    JavaAstNode javaAstNode = queryJavaAstNode(javaAstNodeIdLong);

    List<JavaDocComment> javaDocComments = queryJavaDocComments(javaAstNode);

    if (!javaDocComments.isEmpty()) {
      sb
        .append("<div class=\"main-doc\">")
        .append(javaDocComments.get(0).getContent()
          .replace("\n", "<br/>"))
        .append("</div>");
    }

    switch (javaAstNode.getSymbolType()) {
      case TYPE:
      case ENUM:
        //--- Data members ---//

        List<AstNodeInfo> constructors =
          getReferences(
            javaAstNodeId, ReferenceType.CONSTRUCTOR.ordinal(),
            new ArrayList<>()
          );
        List<AstNodeInfo> methods =
          getReferences(
            javaAstNodeId, ReferenceType.METHOD.ordinal(), new ArrayList<>()
          );


        getDocumentationForAstNodeInfos(sb, constructors);
        getDocumentationForAstNodeInfos(sb, methods);
        break;
    }

    return sb.toString();
  }

  private void getDocumentationForAstNodeInfos(
    StringBuilder sb, List<AstNodeInfo> astNodeInfos)
  {
    astNodeInfos.forEach(nodeInfo -> {
      sb
        .append("<div class=\"group\"><div class=\"signature\">");

      //--- Add tags ---/

      nodeInfo.tags.forEach(tag -> {
        if (tag.equals("public") ||
          tag.equals("private") ||
          tag.equals("protected"))
        {
          sb
            .append("<span class=\"icon-visibility icon-")
            .append(tag)
            .append("\"></span>");
        } else {
          sb
            .append("<span class=\"tag tag-")
            .append(tag).append("\" title=\"")
            .append(tag).append("\">")
            .append(Character.toUpperCase(tag.charAt(0)))
            .append("</span>");
        }
      });

      sb
        .append(
          nodeInfo.astNodeValue
            .replace("\n", "<br/>"))
        .append("</div>");

      //--- Query documentation of members ---//

      List<JavaDocComment> memberDocComments =
        queryJavaDocComments(nodeInfo.entityHash);

      if (!memberDocComments.isEmpty())
        sb
          .append(
            memberDocComments.get(0).getContent()
              .replace("\n", "<br/>")
          );

      sb.append("</div>");
    });
  }

  @Override
  public Map<String, Integer> getReferenceTypes(String javaAstNodeId) {
    long javaAstNodeIdLong = Long.parseLong(javaAstNodeId);
    HashMap<String, Integer> referenceTypes = new HashMap<>();
    JavaAstNode javaAstNode = queryJavaAstNode(javaAstNodeIdLong);

    referenceTypes.put("Definition", ReferenceType.DEFINITION.ordinal());
    referenceTypes.put("Declaration", ReferenceType.DECLARATION.ordinal());
    referenceTypes.put("Usage", ReferenceType.USAGE.ordinal());

    switch (javaAstNode.getSymbolType()) {
      case CONSTRUCTOR:
        referenceTypes.put(
          "This calls", ReferenceType.THIS_CALLS.ordinal());
        referenceTypes.put(
          "Callee", ReferenceType.CALLEE.ordinal());
        referenceTypes.put(
          "Caller", ReferenceType.CALLER.ordinal());
        referenceTypes.put(
          "Parameters", ReferenceType.PARAMETER.ordinal());
        referenceTypes.put(
          "Local variables", ReferenceType.LOCAL_VAR.ordinal());
        break;
      case METHOD:
        referenceTypes.put(
          "This calls", ReferenceType.THIS_CALLS.ordinal());
        referenceTypes.put(
          "Callee", ReferenceType.CALLEE.ordinal());
        referenceTypes.put(
          "Caller", ReferenceType.CALLER.ordinal());
        referenceTypes.put(
          "Parameters", ReferenceType.PARAMETER.ordinal());
        referenceTypes.put(
          "Local variables", ReferenceType.LOCAL_VAR.ordinal());
        referenceTypes.put(
          "Overrides", ReferenceType.OVERRIDE.ordinal());
        referenceTypes.put(
          "Overridden by", ReferenceType.OVERRIDDEN_BY.ordinal());
        referenceTypes.put(
          "Implements", ReferenceType.IMPLEMENT.ordinal());
        referenceTypes.put(
          "Implemented by", ReferenceType.IMPLEMENTED_BY.ordinal());
        referenceTypes.put(
          "Return type", ReferenceType.RETURN_TYPE.ordinal());
        break;
      case VARIABLE:
        referenceTypes.put(
          "Reads", ReferenceType.READ.ordinal());
        referenceTypes.put(
          "Writes", ReferenceType.WRITE.ordinal());
        referenceTypes.put(
          "Type", ReferenceType.TYPE.ordinal());
        break;
      case TYPE:
        referenceTypes.put(
          "Inherits from", ReferenceType.INHERIT_FROM.ordinal());
        referenceTypes.put(
          "Inherited by", ReferenceType.INHERIT_BY.ordinal());
        referenceTypes.put(
          "Initializer", ReferenceType.INITIALIZER.ordinal());
        referenceTypes.put(
          "Inner type", ReferenceType.INNER_TYPE.ordinal());
        referenceTypes.put(
          "Constructor", ReferenceType.CONSTRUCTOR.ordinal());
        referenceTypes.put(
          "Data member", ReferenceType.DATA_MEMBER.ordinal());
        referenceTypes.put(
          "Method", ReferenceType.METHOD.ordinal());
        break;
      case ENUM:
        referenceTypes.put(
          "Inherits from", ReferenceType.INHERIT_FROM.ordinal());
        referenceTypes.put(
          "Initializer", ReferenceType.INITIALIZER.ordinal());
        referenceTypes.put(
          "Inner type", ReferenceType.INNER_TYPE.ordinal());
        referenceTypes.put(
          "Enum constants", ReferenceType.ENUM_CONSTANTS.ordinal());
        referenceTypes.put(
          "Constructor", ReferenceType.CONSTRUCTOR.ordinal());
        referenceTypes.put(
          "Data member", ReferenceType.DATA_MEMBER.ordinal());
        referenceTypes.put(
          "Method", ReferenceType.METHOD.ordinal());
        break;
      case INITIALIZER:
        referenceTypes.put(
          "Local variables", ReferenceType.LOCAL_VAR.ordinal());
    }

    return referenceTypes;
  }

  @Override
  public int getReferenceCount(String javaAstNodeId, int referenceId) {
    long javaAstNodeIdLong = Long.parseLong(javaAstNodeId);
    JavaAstNode javaAstNode = queryJavaAstNode(javaAstNodeIdLong);

    switch (ReferenceType.values()[referenceId]) {
      case DEFINITION:
        return queryDefinitionNodes(javaAstNode).size();
      case DECLARATION:
        return queryVisibleDeclarationNodes(javaAstNode).size();
      case USAGE:
        return queryUsageNodes(javaAstNode).size();
      case THIS_CALLS:
        return queryCallNodes(javaAstNode).size();
      case CALLEE:
        return queryCalleeNodes(javaAstNode).size();
      case CALLER:
        return queryCallerNodes(javaAstNode).size();
      case PARAMETER:
        return queryParameterNodes(javaAstNode).size();
      case LOCAL_VAR:
        return queryLocalVarNodes(javaAstNode).size();
      case RETURN_TYPE:
        return queryReturnTypeNodes(javaAstNode).size();
      case OVERRIDE:
        return queryRelationNodes(
          javaAstNode, RelationKind.OVERRIDE, false).size();
      case OVERRIDDEN_BY:
        return queryRelationNodes(
          javaAstNode, RelationKind.OVERRIDE, true).size();
      case IMPLEMENT:
        return queryRelationNodes(
          javaAstNode, RelationKind.IMPLEMENT, false).size();
      case IMPLEMENTED_BY:
        return queryRelationNodes(
          javaAstNode, RelationKind.IMPLEMENT, true).size();
      case READ:
        return queryReadNodes(javaAstNode).size();
      case WRITE:
        return queryWriteNodes(javaAstNode).size();
      case TYPE:
        return queryTypeNodes(javaAstNode).size();
      case INHERIT_FROM:
        return queryInheritFromNodes(javaAstNode).size();
      case INHERIT_BY:
        return queryInheritedByNodes(javaAstNode).size();
      case INNER_TYPE:
        return queryJavaMemberTypeDefinitionNodes(
          javaAstNode, true,
          MemberTypeKind.TYPE, MemberTypeKind.ENUM
        ).size();
      case INITIALIZER:
        return queryJavaInitializerNodes(javaAstNode).size();
      case CONSTRUCTOR:
        return queryJavaMemberTypeDefinitionNodes(
          javaAstNode, false, MemberTypeKind.CONSTRUCTOR).size();
      case DATA_MEMBER:
        return queryJavaMemberTypeDefinitionNodes(
          javaAstNode, false, MemberTypeKind.FIELD).size();
      case METHOD:
        return queryJavaMemberTypeDefinitionNodes(
          javaAstNode, false, MemberTypeKind.METHOD).size();
      case ENUM_CONSTANTS:
        return queryJavaEnumConstantNodes(javaAstNode).size();
    }

    return 0;
  }

  @Override
  public List<AstNodeInfo> getReferences(
    String javaAstNodeId, int referenceId, List<String> tags)
  {
    long javaAstNodeIdLong = Long.parseLong(javaAstNodeId);
    JavaAstNode javaAstNode = queryJavaAstNode(javaAstNodeIdLong);
    List<JavaAstNode> javaAstNodes = new ArrayList<>();
    List<AstNodeInfo> javaAstNodeInfos;

    switch (ReferenceType.values()[referenceId]) {
      case DEFINITION:
        javaAstNodes = queryDefinitionNodes(javaAstNode);
        break;
      case DECLARATION:
        javaAstNodes = queryVisibleDeclarationNodes(javaAstNode);
        break;
      case USAGE:
        javaAstNodes = queryUsageNodes(javaAstNode);
        break;
      case THIS_CALLS:
        javaAstNodes = queryCallNodes(javaAstNode);
        break;
      case CALLEE:
        javaAstNodes = queryCalleeNodes(javaAstNode);
        break;
      case CALLER:
        javaAstNodes = queryCallerNodes(javaAstNode);
        break;
      case PARAMETER:
        javaAstNodes = queryParameterNodes(javaAstNode);
        break;
      case LOCAL_VAR:
        javaAstNodes = queryLocalVarNodes(javaAstNode);
        break;
      case RETURN_TYPE:
        javaAstNodes = queryReturnTypeNodes(javaAstNode);
        break;
      case OVERRIDE:
        javaAstNodes = queryRelationNodes(
          javaAstNode, RelationKind.OVERRIDE, false);
        break;
      case OVERRIDDEN_BY:
        javaAstNodes = queryRelationNodes(
          javaAstNode, RelationKind.OVERRIDE, true);
        break;
      case IMPLEMENT:
        javaAstNodes = queryRelationNodes(
          javaAstNode, RelationKind.IMPLEMENT, false);
        break;
      case IMPLEMENTED_BY:
        javaAstNodes = queryRelationNodes(
          javaAstNode, RelationKind.IMPLEMENT, true);
        break;
      case READ:
        javaAstNodes = queryReadNodes(javaAstNode);
        break;
      case WRITE:
        javaAstNodes = queryWriteNodes(javaAstNode);
        break;
      case TYPE:
        javaAstNodes = queryTypeNodes(javaAstNode);
        break;
      case INHERIT_FROM:
        javaAstNodes = queryInheritFromNodes(javaAstNode);
        break;
      case INHERIT_BY:
        javaAstNodes = queryInheritedByNodes(javaAstNode);
        break;
      case INNER_TYPE:
        javaAstNodes =
          queryJavaMemberTypeDefinitionNodes(
            javaAstNode, true,
            MemberTypeKind.TYPE, MemberTypeKind.ENUM
          );
        break;
      case INITIALIZER:
        javaAstNodes = queryJavaInitializerNodes(javaAstNode);
        break;
      case CONSTRUCTOR:
        javaAstNodes =
          queryJavaMemberTypeDefinitionNodes(
            javaAstNode, false, MemberTypeKind.CONSTRUCTOR);
        break;
      case DATA_MEMBER:
        javaAstNodes =
          queryJavaMemberTypeDefinitionNodes(
            javaAstNode, false, MemberTypeKind.FIELD);
        break;
      case METHOD:
        javaAstNodes =
          queryJavaMemberTypeDefinitionNodes(
            javaAstNode, false, MemberTypeKind.METHOD);
        break;
      case ENUM_CONSTANTS:
        javaAstNodes = queryJavaEnumConstantNodes(javaAstNode);
        break;
    }

    javaAstNodeInfos = createAstNodeInfos(javaAstNodes);

    return javaAstNodeInfos;
  }

  @Override
  public Map<String, Integer> getFileReferenceTypes() {
    Map<String, Integer> fileReferenceTypes = new HashMap<>();

    fileReferenceTypes.put("Import", FileReferenceType.IMPORTS.ordinal());
    fileReferenceTypes.put("Type", FileReferenceType.TYPES.ordinal());
    fileReferenceTypes.put("Constructor",
      FileReferenceType.CONSTRUCTORS.ordinal());
    fileReferenceTypes.put("Method", FileReferenceType.METHODS.ordinal());

    return fileReferenceTypes;
  }

  @Override
  public int getFileReferenceCount(String fileId, int referenceId) {
    long fileIdLong = Long.parseUnsignedLong(fileId);

    switch (FileReferenceType.values()[referenceId]) {
      case IMPORTS:
        return queryJavaImportNodesInFile(fileIdLong).size();
      case TYPES:
        return queryJavaTypeNodesInFile(fileIdLong).size();
      case CONSTRUCTORS:
        return queryJavaConstructorNodesInFile(fileIdLong).size();
      case METHODS:
        return queryJavaMethodNodesInFile(fileIdLong).size();
    }

    return 0;
  }

  @Override
  public List<AstNodeInfo> getFileReferences(String fileId, int referenceId) {
    long fileIdLong = Long.parseUnsignedLong(fileId);
    List<JavaAstNode> javaAstNodes = new ArrayList<>();
    List<AstNodeInfo> javaAstNodeInfos;

    switch (FileReferenceType.values()[referenceId]) {
      case IMPORTS:
        javaAstNodes = queryJavaImportNodesInFile(fileIdLong);
        break;
      case TYPES:
        javaAstNodes = queryJavaTypeNodesInFile(fileIdLong);
        break;
      case CONSTRUCTORS:
        javaAstNodes = queryJavaConstructorNodesInFile(fileIdLong);
        break;
      case METHODS:
        javaAstNodes = queryJavaMethodNodesInFile(fileIdLong);
        break;
    }

    javaAstNodeInfos = createAstNodeInfos(javaAstNodes);

    return javaAstNodeInfos;
  }

  @Override
  public Map<String, Integer> getDiagramTypes(String javaAstNodeId) {
    JavaAstNode javaAstNode = queryJavaAstNode(Long.parseLong(javaAstNodeId));
    HashMap<String, Integer> diagramTypes = new HashMap<>();

    switch (javaAstNode.getSymbolType())
    {
      case CONSTRUCTOR:
      case METHOD:
        diagramTypes.put(
          "Method call diagram", DiagramType.METHOD_CALL.ordinal());
        break;
      case TYPE:
      case ENUM:
        diagramTypes.put(
          "Detailed class diagram", DiagramType.DETAILED_CLASS.ordinal()
        );
        diagramTypes.put(
          "Class collaboration diagram",
          DiagramType.CLASS_COLLABORATION.ordinal()
        );
        break;
    }

    return diagramTypes;
  }

  @Override
  public String getDiagram(String javaAstNodeId, int diagramId) {
    JavaAstNode javaAstNode = queryJavaAstNode(Long.parseLong(javaAstNodeId));

    switch (DiagramType.values()[diagramId])
    {
      case METHOD_CALL:
        break;

      case DETAILED_CLASS:
        break;

      case CLASS_COLLABORATION:
        break;
    }

    return "";
  }

  @Override
  public List<SyntaxHighlight> getSyntaxHighlight(
    FileRange fileRange, List<String> content)
  {
    List<SyntaxHighlight> syntaxHighlights = new ArrayList<>();
    /*
    Pattern specialChars = Pattern.compile("[-\\[\\]{}()*+?.,\\^$|#\\s]");
    List<JavaAstNode> javaAstNodes = queryJavaAstNodesByFileRange(fileRange);

    javaAstNodes.stream()
      .filter(n -> !n.getAstValue().isEmpty())
      .forEach(n -> {
        Matcher matcher = specialChars.matcher(n.getAstValue());
        String sanitizedAstValue =
          matcher.replaceAll(matchResult -> "\\\\" + matchResult.group());
        String reg = "\\b" + sanitizedAstValue + "\\b";
        int startLine = (int) n.getLocation_range_start_line();
        int endLine = (int) n.getLocation_range_end_line();

        for (int i = startLine - 1; i < endLine && i < content.size(); ++i) {
          Pattern wordPattern = Pattern.compile(reg);
          Matcher wordMatcher = wordPattern.matcher(content.get(i));

          while (wordMatcher.find()) {
            MatchResult matchResult = wordMatcher.toMatchResult();
            SyntaxHighlight syntax = new SyntaxHighlight();
            Range range = new Range();
            Position startPosition = new Position();
            Position endPosition = new Position();

            startPosition.line = i + 1;
            startPosition.column = matchResult.start() + 1;
            endPosition.line = i + 1;
            endPosition.column = matchResult.end() + 1;

            range.startpos = startPosition;
            range.endpos = endPosition;

            syntax.range = range;

            String symbolClass = "cm-" + n.getSymbolType().getValue();

            syntax.className = symbolClass + " " +
              symbolClass + "-" + n.getAstType().getValue();

            syntaxHighlights.add(syntax);
          }
        }
      });
    */
    return syntaxHighlights;
  }

  private List<AstNodeInfo> createAstNodeInfos(List<JavaAstNode> javaAstNodes) {
    Map<Long, List<String>> tags = getTags(javaAstNodes);

    return javaAstNodes.stream()
      .map(p -> createAstNodeInfo(p, tags))
      .sorted((n1, n2) -> {
        Integer line1 = n1.range.range.startpos.line;
        Integer line2 = n2.range.range.startpos.line;
        int lineComp = line1.compareTo(line2);

        if (lineComp != 0) {
          return lineComp;
        }

        Integer col1 = n1.range.range.startpos.column;
        Integer col2 = n2.range.range.startpos.column;
        return col1.compareTo(col2);
      })
      .collect(Collectors.toList());
  }

  private AstNodeInfo createAstNodeInfo(JavaAstNode javaAstNode) {
    AstNodeInfo astNodeInfo = new AstNodeInfo();
    FileRange fileRange = getFileRange(javaAstNode);

    astNodeInfo.id = String.valueOf(javaAstNode.getId());
    astNodeInfo.entityHash = javaAstNode.getEntityHash();
    astNodeInfo.astNodeType = javaAstNode.getAstType().getName();
    astNodeInfo.symbolType = javaAstNode.getSymbolType().getName();
    astNodeInfo.astNodeValue = javaAstNode.getAstValue();
    astNodeInfo.range = fileRange;

    return astNodeInfo;
  }

  private AstNodeInfo createAstNodeInfo(
    JavaAstNode javaAstNode, Map<Long, List<String>> tags)
  {
    AstNodeInfo astNodeInfo = createAstNodeInfo(javaAstNode);

    astNodeInfo.tags = tags.get(javaAstNode.getId());

    return astNodeInfo;
  }

  private Map<Long, List<String>> getTags(List<JavaAstNode> javaAstNodes) {
    Map<Long, List<String>> tags = new HashMap<>();

    javaAstNodes.forEach(node -> {
      List<JavaAstNode> definitions = queryDefinitionNodes(node);
      JavaAstNode definition =
        definitions.isEmpty() ? node : definitions.get(0);

      switch (node.getSymbolType()) {
        case TYPE:
          putTags(node, definition, MemberTypeKind.TYPE, tags);
          break;
        case CONSTRUCTOR:
          putTags(node, definition, MemberTypeKind.CONSTRUCTOR, tags);
          break;
        case VARIABLE:
          putTags(node, definition, MemberTypeKind.FIELD, tags);
          break;
        case METHOD:
          putTags(node, definition, MemberTypeKind.METHOD, tags);
          break;
        case ENUM:
          putTags(node, definition, MemberTypeKind.ENUM, tags);
          break;
        case ENUM_CONSTANT:
          putTags(node, definition, MemberTypeKind.ENUM_CONSTANT, tags);
          break;
      }
    });

    return tags;
  }

  private void putTags(
    JavaAstNode javaAstNode, JavaAstNode definition,
    MemberTypeKind memberTypeKind, Map<Long, List<String>> tags)
  {
    List<JavaMemberType> javaMemberTypes =
      queryJavaMemberTypes(javaAstNode, definition, memberTypeKind);

    javaMemberTypes.forEach(m -> {
      long nodeId = javaAstNode.getId();
      if (!tags.containsKey(nodeId)) {
        tags.put(
          nodeId,
          new ArrayList<>(
            Collections.singleton(m.getVisibility().getName())
          )
        );
      } else {
        tags.get(nodeId).add(m.getVisibility().getName());
      }
    });
  }

  private FileRange getFileRange(JavaAstNode javaAstNode) {
    FileRange fileRange = new FileRange();
    Range range = new Range();
    Position startPosition = new Position(
      (int) javaAstNode.getLocation_range_start_line(),
      (int) javaAstNode.getLocation_range_start_column()
    );
    Position endPosition = new Position(
      (int) javaAstNode.getLocation_range_end_line(),
      (int) javaAstNode.getLocation_range_end_column()
    );

    range.startpos = startPosition;
    range.endpos = endPosition;

    fileRange.file = Long.toUnsignedString(javaAstNode.getLocation_file());
    fileRange.range = range;

    return fileRange;
  }
}