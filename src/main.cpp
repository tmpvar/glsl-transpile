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

  void dumpSlots() {
	cout << "slots:" << endl;
	for (size_t i =0; i<this->slots.size(); i++) {
	  ProgramSlot *s = this->slots[i];
	  printf("  %i: binding type: %s name: %s type: %s in: %d out: %d\n",
		i,
		s->binding_type.c_str(),
		s->name.c_str(),
		s->type.c_str(),
		s->input,
		s->output
	  );

	  for (const auto &it : s->structure) {
		printf("    - %s: %s (%ix%i)\n", it->name.c_str(), it->type.c_str(), it->cols, it->rows);
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
	}
	return false;
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
		/*case glslang::EsdCube:break;
		case glslang::EsdRect:break;
		case glslang::EsdBuffer:break;*/
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


int main(void) {

  if (!glslang::InitializeProcess()) {
	   return 1;
  }

  glslang::TShader *shader = new glslang::TShader(EShLangCompute);
  glslang::TProgram *program = new glslang::TProgram();
  shader->setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
  shader->setEnvTarget(glslang::EShTargetNone, glslang::EShTargetSpv_1_4);

  const char *shaderString[1] = {
	"uniform vec3 aVector;"
	"uniform sampler1D my1DTexture;"
	"uniform sampler2D myTexture;"
	"uniform sampler3D myArrayTexture[2];"
	"uniform vec3 anUnusedVector;"
	"uniform mat4 anUnusedMat4;"
	"layout(rgba32f) uniform image2D img1;"
	"layout(local_size_x = 32, local_size_y = 8, local_size_z = 1) in;"
	"layout (std430) buffer blueNoiseBuffer {"
	"  vec4 blueNoise[];"
	"}; "
	"layout (std430) buffer unusedBuffer {"
	"  float aFloat;"
	"  uint aUint;"
	"  vec4 unused[];"
	"};"
	"void main() {"
	"  blueNoise[gl_GlobalInvocationID.x] = vec4(aVector, 1.0);"
	"}\0"
  };
  // TODO: use setStringsWithLengths for safety
  shader->setStrings(&shaderString[0], 1);
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
	std::cout << "shader failed to compile: " << std::endl << shader->getInfoLog() << std::endl;
	return 1;
  }

  program->addShader(shader);
  if (!program->link(messages)) {
	std::cout << "program failed to link: " << std::endl << program->getInfoLog() << std::endl;
	return 1;
  }

  glslang::TIntermediate *ast = program->getIntermediate(EShLangCompute);

  LinkerObjectsIterator *it = new LinkerObjectsIterator();
  ast->getTreeRoot()->traverse(it);
  it->dumpSlots();

  program->buildReflection(
	EShReflectionAllBlockVariables |
	EShReflectionIntermediateIO | 0xFF
  );


  for (int stage = 0; stage < EShLangCount; ++stage) {
	if (program->getIntermediate((EShLanguage)stage)) {
	  std::vector<unsigned int> spirv;
	  std::string warningsErrors;
	  spv::SpvBuildLogger logger;
	  glslang::SpvOptions spvOptions;
	  //if (Options & EOptionDebug)
	  //	spvOptions.generateDebugInfo = true;
	  spvOptions.disableOptimizer = false;//(Options & EOptionOptimizeDisable) != 0;
	  spvOptions.optimizeSize = false;//(Options & EOptionOptimizeSize) != 0;
	  spvOptions.disassemble = false;
	  spvOptions.validate = true;

	  glslang::GlslangToSpv(*program->getIntermediate((EShLanguage)stage), spirv, &logger, &spvOptions);

	  

	  cout << "SPIRV " << logger.getAllMessages().c_str();
	  spv::Disassemble(std::cout, spirv);
	  //glslang::OutputSpvBin(spirv, GetBinaryName((EShLanguage)stage));
	}
  }
  return 1;
}
