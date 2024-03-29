// glslang
#define AMD_EXTENSIONS
#define NV_EXTENSIONS
#include "ResourceLimits.h"
#include "glslang/Public/ShaderLang.h"
#include "glslang/OSDependent/osinclude.h"
#include "glslang/Include/Common.h"

// what a nightmare, defining NOMINMAX doesn't work anymore???
#undef min
#undef max
#include "glslang/MachineIndependent/localintermediate.h"
#include "glslang/MachineIndependent/reflection.h"
#include "glslang/Include/Types.h"

#include "SPIRV/spirv.hpp"
#include "SPIRV/SpvTools.h"
#include "SPIRV/disassemble.h"
#include "SPIRV/GlslangToSpv.h"
#include "SPIRV/GLSL.std.450.h"
#include "SPIRV/doc.h"
#include "SPIRV/disassemble.h"
#include "SPIRV/Logger.h"

#include "spirv_glsl.hpp"
#include "spirv_hlsl.hpp"
#include "spirv_msl.hpp"



#include "json.hpp"
#include "argh.h"

#include <iostream>
using namespace std;

std::string pad(int l) {
  std::string o = "";
  for (int i = 0; i < l; i++) {
    o+="  ";
  }
  return o;
}
using namespace std;

class SlotStructEntry {
public:
  string name;
  string type;
  int rows = 1;
  int cols = 1;
};

class ProgramSlot {
  public:
  bool input = false;
  bool output = false;

  // for arrays where -1 is unbounded (SSBO) and 1+ is the static array length
  int length = 0;

  std::string binding_type;
  std::string name;
  std::string packing;
  std::string type;

  std::vector<SlotStructEntry *> structure;
};

class LinkerObjectsIterator : public glslang::TIntermTraverser{
public:
  std::vector<ProgramSlot *> slots;

  void dumpSlots(nlohmann::json &obj) {
    // cout << "slots:" << endl;
    for (size_t i =0; i<this->slots.size(); i++) {
      ProgramSlot *s = this->slots[i];
      obj["slots"][i]["binding_type"] = s->binding_type;
      obj["slots"][i]["name"] = s->name;
      obj["slots"][i]["input"] = s->input;
      obj["slots"][i]["output"] = s->output;

      for (size_t j = 0; j< s->structure.size(); j++) {
        SlotStructEntry *field = s->structure[j];
        obj["slots"][i]["fields"][j]["name"] = field->name;
        obj["slots"][i]["fields"][j]["type"] = field->type;
        obj["slots"][i]["fields"][j]["cols"] = field->cols;
        obj["slots"][i]["fields"][j]["rows"] = field->rows;
      }
    }
  }


  LinkerObjectsIterator(): TIntermTraverser(false, true, false) {};

  bool visitAggregate(glslang::TVisit, glslang::TIntermAggregate* node) {
    switch (node->getOp()) {
      case glslang::EOpLinkerObjects:
        return true;
      case glslang::EOpSequence:
        return true;
      default:
        return false;
    }
  }

  void visitSymbol(glslang::TIntermSymbol* node) {
    glslang::TQualifier q = node->getQualifier();
    if (!q.isIo()) {
      return;
    }

    ProgramSlot *s = new ProgramSlot;


    switch (q.storage) {
      case glslang::EvqUniform:
        s->binding_type = "uniform";
        s->name = node->getName().c_str();
        s->input = true;
        s->output = false;
        break;
      case glslang::EvqBuffer:
        s->binding_type = "buffer";
        s->name = node->getType().getTypeName().c_str();
        s->packing = glslang::TQualifier::getLayoutPackingString(q.layoutPacking);

        if (q.readonly) {
          s->input = true;
          s->output = false;
        } else if (q.writeonly) {
          s->input = false;
          s->output = true;
        } else {
          s->input = true;
          s->output = true;
        }
        break;
      default:
        int a = 1;
        delete s;
        return;
    }

    this->slots.push_back(s);

    if (node->getType().isVector()) {
      s->type = "vector";
      SlotStructEntry *entry = new SlotStructEntry;

      //entry->name.assign(t->getFieldName().c_str(), t->getFieldName().size());
      const glslang::TString str = node->getType().getBasicTypeString();
      entry->type.assign(str.c_str(), str.size());
      entry->cols = node->getType().getVectorSize();
      s->structure.push_back(entry);
    }

    if (node->getType().isMatrix()) {
      s->type = "matrix";
      SlotStructEntry *entry = new SlotStructEntry;
      const glslang::TString str = node->getType().getBasicTypeString();
      entry->type.assign(str.c_str(), str.size());
      entry->rows = node->getType().getMatrixRows();
      entry->cols = node->getType().getMatrixCols();
      s->structure.push_back(entry);
    }

    if (node->getType().isStruct()) {
      s->type = "struct";
      const glslang::TTypeList *structure = node->getType().getStruct();

      for (glslang::TTypeList::const_iterator tl = structure->begin(); tl != structure->end(); tl++) {
      SlotStructEntry *entry = new SlotStructEntry;
      glslang::TType *t = (*tl).type;

      entry->name.assign(t->getFieldName().c_str(), t->getFieldName().size());
      entry->type.assign(t->getBasicTypeString().c_str(), t->getBasicTypeString().size());

      entry->cols = t->getVectorSize();
      s->structure.push_back(entry);
      }
    }

    if (node->getType().isTexture() || node->getType().isImage()) {
      s->type = "texture";
      const glslang::TSampler &sampler = node->getType().getSampler();
      SlotStructEntry *entry = new SlotStructEntry;

      if (sampler.isPureSampler()) {
        entry->type = "sampler";
      } else if (sampler.isTexture()) {
        entry->type = "texture";
      } else if (sampler.isShadow()) {
        entry->type = "shadow";
      } else if (sampler.isImage()) {
        entry->type = "image";
      }

      switch (sampler.dim) {
        case glslang::Esd1D: entry->rows = 1; break;
        case glslang::Esd2D: entry->rows = 2; break;
        case glslang::Esd3D: entry->rows = 3; break;
        default: break;

        // case glslang::EsdCube:
        //   break;
        // case glslang::EsdRect:
        //   break;
        // case glslang::EsdBuffer:
        //   break;
      }
      entry->cols = sampler.getVectorSize();
      s->structure.push_back(entry);
    }

    if (node->getConstSubtree()) {
      incrementDepth(node);
      node->getConstSubtree()->traverse(this);
      decrementDepth();
    }
  }
};

enum OutputLang {
  NONE = 0,
  GLSL,
  HLSL,
  MSL
};

int main(int, char* argv[]) {
  argh::parser cmdl(argv);
  string langOption;
  cmdl(1) >> langOption;

  OutputLang lang;
  if (langOption == "msl") {
    lang = OutputLang::MSL;
  } else if (langOption == "hlsl") {
    lang = OutputLang::HLSL;
  } else if (langOption == "glsl") {
    lang = OutputLang::GLSL;
  } else {
    cerr << "Usage: cat source.glsl | glsl-transpile (msl|glsl|hlsl)" << endl;
    return 1;
  }

  if (!glslang::InitializeProcess()) {
     return 1;
  }

  nlohmann::json obj;


  ios::sync_with_stdio(false);
  cin >> noskipws;
  istream_iterator<char> stdinIterator(std::cin);
  istream_iterator<char> stdinEnd;
  string jsonInput(stdinIterator, stdinEnd);

  glslang::TShader *shader = new glslang::TShader(EShLangCompute);
  glslang::TProgram *program = new glslang::TProgram();
  shader->setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
  shader->setEnvTarget(glslang::EShTargetNone, glslang::EShTargetSpv_1_4);

  const char *shaderSources[1] = {
    jsonInput.c_str()
  };

  int shaderLengths[1] = {
    static_cast<int>(jsonInput.size())
  };

  shader->setStringsWithLengths(shaderSources, shaderLengths, 1);
  TBuiltInResource Resources = glslang::DefaultTBuiltInResource;
  EShMessages messages = EShMessages(0);
  // TODO: shader->preprocess to load in the includes
  bool parsed = shader->parse(
    &Resources,
    glslang::EShTargetOpenGL_450,
    true, // forward compat
    messages
  );

  if (!parsed) {
    obj["error"]["type"] = "parse";
    obj["error"]["log"] = shader->getInfoLog();
    std::cout << obj.dump(4) << std::endl;
    return 1;
  }

  program->addShader(shader);
  if (!program->link(messages)) {
    obj["error"]["type"] = "link";
    obj["error"]["log"] = shader->getInfoLog();
    std::cout << obj.dump(4) << std::endl;

    std::cout << "program failed to link: " << std::endl << program->getInfoLog() << std::endl;
    return 1;
  }

  glslang::TIntermediate *ast = program->getIntermediate(EShLangCompute);

  LinkerObjectsIterator *it = new LinkerObjectsIterator();
  ast->getTreeRoot()->traverse(it);
  it->dumpSlots(obj);

  program->buildReflection(
    EShReflectionAllBlockVariables |
    EShReflectionIntermediateIO | 0xFF
  );

  int active_stage = 0;
  for (int stage = 0; stage < EShLangCount; ++stage) {
    if (program->getIntermediate((EShLanguage)stage)) {
      string stageName = glslang::StageName((EShLanguage)stage);
      std::vector<unsigned int> spirv;
      std::string warningsErrors;
      spv::SpvBuildLogger logger;
      glslang::SpvOptions spvOptions;

      spvOptions.generateDebugInfo = true;
      spvOptions.disableOptimizer = false;
      spvOptions.optimizeSize = false;
      spvOptions.disassemble = false;
      spvOptions.validate = true;

      glslang::GlslangToSpv(*program->getIntermediate((EShLanguage)stage), spirv, &logger, &spvOptions);
      obj["stages"][stageName]["log"] = logger.getAllMessages();

      spirv[1] = SPV_VERSION;

      if (lang == OutputLang::GLSL) {
          spirv_cross::CompilerGLSL glsl(spirv);
          spirv_cross::CompilerGLSL::Options glslOptions;

          glslOptions.version = 460;
          glslOptions.es = false;
          glsl.set_common_options(glslOptions);

          obj["stages"][stageName]["source"] = glsl.compile();
      } else if (lang == OutputLang::HLSL) {
          spirv_cross::CompilerHLSL hlsl(spirv);
          spirv_cross::CompilerHLSL::Options hlslOptions;
          hlslOptions.shader_model = 50;
          hlsl.set_hlsl_options(hlslOptions);
          obj["stages"][stageName]["source"] = hlsl.compile();
      } else if (lang == OutputLang::MSL) {
          spirv_cross::CompilerMSL msl(spirv);
          obj["stages"][stageName]["source"] = msl.compile();
      }

      active_stage++;
    }
  }
  std::cout << obj.dump(4) << std::endl;
  return 0;
}
