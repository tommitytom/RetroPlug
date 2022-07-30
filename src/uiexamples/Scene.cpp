#include "Scene.h"

using namespace rp;

bgfx::ShaderHandle loadShader(const char* FILENAME)
{
	std::string fileName = FILENAME;
	std::string shaderPath = "???";

	switch (bgfx::getRendererType()) {
	case bgfx::RendererType::Noop:
	case bgfx::RendererType::Direct3D9:  shaderPath = "shaders/dx9/";   break;
	case bgfx::RendererType::Direct3D11:
	case bgfx::RendererType::Direct3D12: shaderPath = "shaders/dx11/";  break;
	case bgfx::RendererType::Gnm:        shaderPath = "shaders/pssl/";  break;
	case bgfx::RendererType::Metal:      shaderPath = "shaders/metal/"; break;
	case bgfx::RendererType::OpenGL:     shaderPath = "shaders/glsl/";  break;
	case bgfx::RendererType::OpenGLES:   shaderPath = "shaders/essl/";  break;
	case bgfx::RendererType::Vulkan:     shaderPath = "shaders/spirv/"; break;
	}

	shaderPath += fileName;

	FILE* file = fopen(shaderPath.c_str(), "rb");
	fseek(file, 0, SEEK_END);
	long fileSize = ftell(file);
	fseek(file, 0, SEEK_SET);

	const bgfx::Memory* mem = bgfx::alloc(fileSize + 1);
	fread(mem->data, 1, fileSize, file);
	mem->data[mem->size - 1] = '\0';
	fclose(file);

	return bgfx::createShader(mem);
}

void Scene::init() {
	bgfx::VertexLayout pcvDecl;
	pcvDecl.begin()
		.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
		.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
		.end();

	_vert = bgfx::createVertexBuffer(bgfx::makeRef(cubeVertices, sizeof(cubeVertices)), pcvDecl);
	_ind = bgfx::createIndexBuffer(bgfx::makeRef(cubeTriList, sizeof(cubeTriList)));

	bgfx::ShaderHandle vsh = loadShader("vs_cubes.bin");
	bgfx::ShaderHandle fsh = loadShader("fs_cubes.bin");
	_prog = bgfx::createProgram(vsh, fsh, true);

	/*b2Vec2 gravity(0.0f, -10.0f);
	PhysicsWorldSingleton& physicsWorld = _registry.set<PhysicsWorldSingleton>(PhysicsWorldSingleton{ .world = b2World(gravity) });

	entt::entity e = _registry.create();

	b2BodyDef groundBodyDef;
	groundBodyDef.position.Set(0.0f, -10.0f);

	b2PolygonShape groundBox;
	groundBox.SetAsBox(50.0f, 10.0f);

	Box2dUtil::addRigidBody(_registry, e, groundBodyDef, groundBox, 0.0f);

	_viewPort = { -20, -20, 40, 40 };

	b2Vec2 topl(-20, -20);
	b2Vec2 mid(0, 0);
	b2Vec2 pos(-10, 10); // -0.5, 0.5

	f32 wc = 2.0f / _viewPort.w; // 0.05*/


	//bgfx::ShaderHandle frag = loadShader(fs_debug, sizeof(fs_debug), "Debug Fragment Shader");
	//bgfx::ShaderHandle vert = loadShader(vs_debug, sizeof(vs_debug), "Debug Vert Shader");

	//_prog = bgfx::createProgram(vert, frag, true);
}

void Scene::render() {
	const bx::Vec3 at = { 0.0f, 0.0f,  0.0f };
	const bx::Vec3 eye = { 0.0f, 0.0f, -5.0f };
	float view[16];
	bx::mtxLookAt(view, eye, at);
	float proj[16];
	bx::mtxProj(proj, 60.0f, float(WNDW_WIDTH) / float(WNDW_HEIGHT), 0.1f, 100.0f, bgfx::getCaps()->homogeneousDepth);
	bgfx::setViewTransform(0, view, proj);

	/*float mtx[16];
	bx::mtxRotateXY(mtx, counter * 0.01f, counter * 0.01f);
	bgfx::setTransform(mtx);

	bgfx::setVertexBuffer(0, vbh);
	bgfx::setIndexBuffer(ibh);

	bgfx::submit(0, program);*/
	bgfx::frame();
}
