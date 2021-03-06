/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "Rendering/GL/myGL.h"
#include <algorithm>
#include <cctype>
#include "System/mmgr.h"

#include "IModelParser.h"
#include "3DModel.h"
#include "3DModelLog.h"
#include "3DOParser.h"
#include "S3OParser.h"
#include "OBJParser.h"
#include "AssParser.h"
#include "Sim/Misc/CollisionVolume.h"
#include "System/FileSystem/FileSystem.h"
#include "System/Util.h"
#include "System/Log/ILog.h"
#include "System/Exceptions.h"
#include "lib/gml/gml_base.h"
#include "lib/assimp/include/assimp/Importer.hpp"

C3DModelLoader* modelParser = NULL;



static inline ModelType ModelExtToModelType(const C3DModelLoader::ParserMap& parsers, const std::string& ext) {
	if (ext == "3do") { return MODELTYPE_3DO; }
	if (ext == "s3o") { return MODELTYPE_S3O; }
	if (ext == "obj") { return MODELTYPE_OBJ; }

	if (parsers.find(ext) != parsers.end()) {
		return MODELTYPE_ASS;
	}

	// FIXME: this should be a content error?
	return MODELTYPE_OTHER;
}

static inline S3DModelPiece* ModelTypeToModelPiece(const ModelType& type) {
	if (type == MODELTYPE_3DO) { return (new S3DOPiece()); }
	if (type == MODELTYPE_S3O) { return (new SS3OPiece()); }
	if (type == MODELTYPE_OBJ) { return (new SOBJPiece()); }
	// FIXME: SAssPiece is not yet fully implemented
	// if (type == MODELTYPE_ASS) { return (new SAssPiece()); }
	return NULL;
}

static void RegisterAssimpModelParsers(C3DModelLoader::ParserMap& parsers, CAssParser* assParser) {
	std::string extension;
	std::string extensions;
	Assimp::Importer importer;

	// get a ";" separated list of format extensions ("*.3ds;*.lwo;*.mesh.xml;...")
	importer.GetExtensionList(extensions);

	// do not ignore the last extension
	extensions += ";";

	LOG("[%s] supported Assimp model formats: %s",
			__FUNCTION__, extensions.c_str());

	size_t i = 0;
	size_t j = 0;

	// split the list, strip off the "*." extension prefixes
	while ((j = extensions.find(";", i)) != std::string::npos) {
		extension = extensions.substr(i, j - i);
		extension = extension.substr(extension.find("*.") + 2);

		StringToLowerInPlace(extension);

		if (parsers.find(extension) == parsers.end()) {
			parsers[extension] = assParser;
		}

		i = j + 1;
	}
}



C3DModelLoader::C3DModelLoader()
{
	// file-extension should be lowercase
	parsers["3do"] = new C3DOParser();
	parsers["s3o"] = new CS3OParser();
	parsers["obj"] = new COBJParser();

	// FIXME: unify the metadata formats of CAssParser and COBJParser
	RegisterAssimpModelParsers(parsers, new CAssParser());
}


C3DModelLoader::~C3DModelLoader()
{
	// delete model cache
	ModelMap::iterator ci;
	for (ci = cache.begin(); ci != cache.end(); ++ci) {
		S3DModel* model = ci->second;

		DeleteChilds(model->GetRootPiece());
		delete model;
	}
	cache.clear();

	// get rid of Spring's native parsers
	delete parsers["3do"]; parsers.erase("3do");
	delete parsers["s3o"]; parsers.erase("s3o");
	delete parsers["obj"]; parsers.erase("obj");

	if (!parsers.empty()) {
		// delete the shared Assimp parser
		ParserMap::iterator pi = parsers.begin();
		delete pi->second;
	}

	parsers.clear();

	if (GML::SimEnabled() && !GML::ShareLists()) {
		createLists.clear();
		fixLocalModels.clear();
		Update(); // delete remaining local models
	}
}



void CheckModelNormals(const S3DModel* model) {
	const char* modelName = model->name.c_str();
	const char* formatStr =
		"[%s] piece \"%s\" of model \"%s\" has %u (of %u) null-normals!"
		"It will either be rendered fully black or with black splotches!";

	// Warn about models with null normals (they break lighting and appear black)
	for (ModelPieceMap::const_iterator it = model->pieces.begin(); it != model->pieces.end(); ++it) {
		const S3DModelPiece* modelPiece = it->second;
		const char* pieceName = it->first.c_str();

		if (modelPiece->GetVertexCount() == 0)
			continue;

		unsigned int numNullNormals = 0;

		for (unsigned int n = 0; n < modelPiece->GetVertexCount(); n++) {
			numNullNormals += (modelPiece->GetNormal(n).SqLength() < 0.5f);
		}

		if (numNullNormals > 0) {
			LOG_L(L_WARNING, formatStr, __FUNCTION__, pieceName, modelName, numNullNormals, modelPiece->GetVertexCount());
		}
	}
}



S3DModel* C3DModelLoader::Load3DModel(std::string name)
{
	GML_RECMUTEX_LOCK(model); // Load3DModel

	StringToLowerInPlace(name);

	// search in cache first
	ModelMap::iterator ci;
	ParserMap::iterator pi;

	if ((ci = cache.find(name)) != cache.end()) {
		return ci->second;
	}

	// not found in cache, create the model and cache it
	const std::string& fileExt = FileSystem::GetExtension(name);

	if (fileExt.empty()) {
		// fallback (TODO: try all registered extensions?)
		pi = parsers.find("3do");
		name += ".3do";
	} else {
		pi = parsers.find(fileExt);
	}

	if (pi != parsers.end()) {
		IModelParser* p = pi->second;
		S3DModel* model = NULL;
		S3DModelPiece* root = NULL;

		try {
			model = p->Load("objects3d/" + name);
		} catch (const content_error& ex) {
			// crash-dummy
			model = new S3DModel();
			model->type = ModelExtToModelType(parsers, StringToLower(fileExt));
			model->numPieces = 1;
			// give it one dummy piece
			model->SetRootPiece(ModelTypeToModelPiece(model->type));
			model->GetRootPiece()->SetCollisionVolume(new CollisionVolume("box", UpVector * -1.0f, ZeroVector));

			LOG_L(L_WARNING, "could not load model \"%s\" (reason: %s)", name.c_str(), ex.what());
		}

		if ((root = model->GetRootPiece()) != NULL) {
			CreateLists(root);
		}

		cache[name] = model; // cache the model
		model->id = cache.size(); // IDs start with 1

		CheckModelNormals(model);

		return model;
	}

	LOG_L(L_ERROR, "could not find a parser for model \"%s\" (unknown format?)", name.c_str());
	return NULL;
}

void C3DModelLoader::Update() {
	if (GML::SimEnabled() && !GML::ShareLists()) {
		GML_RECMUTEX_LOCK(model); // Update

		for (std::vector<S3DModelPiece*>::iterator it = createLists.begin(); it != createLists.end(); ++it) {
			CreateListsNow(*it);
		}
		createLists.clear();

		for (std::set<LocalModel*>::iterator i = fixLocalModels.begin(); i != fixLocalModels.end(); ++i) {
			(*i)->ReloadDisplayLists();
		}
		fixLocalModels.clear();

		for (std::vector<LocalModel*>::iterator i = deleteLocalModels.begin(); i != deleteLocalModels.end(); ++i) {
			delete *i;
		}
		deleteLocalModels.clear();
	}
}


void C3DModelLoader::DeleteChilds(S3DModelPiece* o)
{
	for (std::vector<S3DModelPiece*>::iterator di = o->childs.begin(); di != o->childs.end(); ++di) {
		DeleteChilds(*di);
	}

	o->childs.clear();
	delete o;
}


void C3DModelLoader::CreateLocalModel(LocalModel* localModel)
{
	const S3DModel* model = localModel->original;
	const S3DModelPiece* root = model->GetRootPiece();

	const bool dlistLoaded = (root->dispListID != 0);

	if (!dlistLoaded && GML::SimEnabled() && !GML::ShareLists()) {
		GML_RECMUTEX_LOCK(model); // CreateLocalModel

		fixLocalModels.insert(localModel);
	}
}

void C3DModelLoader::DeleteLocalModel(LocalModel* localModel)
{
	if (GML::SimEnabled() && !GML::ShareLists()) {
		GML_RECMUTEX_LOCK(model); // DeleteLocalModel

		fixLocalModels.erase(localModel);
		deleteLocalModels.push_back(localModel);
	} else {
		delete localModel;
	}
}


void C3DModelLoader::CreateListsNow(S3DModelPiece* o)
{
	GLuint id = glGenLists(1);
	glNewList(id, GL_COMPILE);
		o->DrawForList();
	glEndList();

	for (std::vector<S3DModelPiece*>::iterator bs = o->childs.begin(); bs != o->childs.end(); ++bs) {
		CreateListsNow(*bs);
	}

	// bind when everything is ready, should be more safe in multithreaded scenarios
	// TODO: still for 100% safety it should use GL_SYNC
	o->dispListID = id;
}


void C3DModelLoader::CreateLists(S3DModelPiece* o) {
	if (GML::SimEnabled() && !GML::ShareLists())
		// already mutex'ed via ::Load3DModel()
		createLists.push_back(o);
	else
		CreateListsNow(o);
}

/******************************************************************************/
/******************************************************************************/
