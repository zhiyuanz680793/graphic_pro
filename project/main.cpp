

#ifdef _WIN32
extern "C" _declspec(dllexport) unsigned int NvOptimusEnablement = 0x00000001;
#endif

#include <GL/glew.h>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <chrono>

#include <labhelper.h>
#include <imgui.h>
#include <imgui_impl_sdl_gl3.h>

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

#include <Model.h>
#include "hdr.h"
#include "fbo.h"
#include "ParticleSystem.h"
#include <stb_image.h>
using std::min;
using std::max;

///////////////////////////////////////////////////////////////////////////////
// Various globals
///////////////////////////////////////////////////////////////////////////////
SDL_Window* g_window = nullptr;
float currentTime = 0.0f;
float previousTime = 0.0f;
float deltaTime = 0.0f;
bool showUI = false;
int windowWidth, windowHeight;

// Mouse input
ivec2 g_prevMouseCoords = { -1, -1 };
bool g_isMouseDragging = false;

///////////////////////////////////////////////////////////////////////////////
// Shader programs
///////////////////////////////////////////////////////////////////////////////
GLuint shaderProgram;       
GLuint simpleShaderProgram; // Shader used to draw the shadow map 
GLuint backgroundProgram; 
GLuint particleShaderProgram; 
//GLuint basicShaderProgram;

///////////////////////////////////////////////////////////////////////////////
// Environment
///////////////////////////////////////////////////////////////////////////////
float environment_multiplier = 1.5f;
GLuint environmentMap, irradianceMap, reflectionMap;
const std::string envmap_base_name = "001";
///////////////////////////////////////////////////////////////////////////////
// Light source copy from labs before
///////////////////////////////////////////////////////////////////////////////
vec3 lightPosition; 
float lightAzimuth = 0.f; 
float lightZenith = 45.f; 
float lightDistance = 55.f; 
bool animateLight = false; 
vec3 point_light_color = vec3(1.f, 1.f, 1.f); 
bool useSpotLight = true; // false 
float innerSpotlightAngle = 17.5f; 
float outerSpotlightAngle = 22.5f; 
float point_light_intensity_multiplier = 10000.0f; 


///////////////////////////////////////////////////////////////////////////////
// Shadow map
///////////////////////////////////////////////////////////////////////////////
enum ClampMode
{
	Edge = 1,
	Border = 2
};
FboInfo shadowMapFB;
int shadowMapResolution = 512;
int shadowMapClampMode = ClampMode::Border; // ClampMode::Edge
bool shadowMapClampBorderShadowed = false;
bool usePolygonOffset = true; // false
bool useSoftFalloff = false;
bool useHardwarePCF = false;
float polygonOffset_factor = 2.0f; // .25f
float polygonOffset_units = 10.0f;  // 1.0f


///////////////////////////////////////////////////////////////////////////////
// Camera parameters.
///////////////////////////////////////////////////////////////////////////////
vec3 cameraPosition(-70.0f, 50.0f, 70.0f);
vec3 cameraDirection = normalize(vec3(0.0f) - cameraPosition);
float cameraSpeed = 10.f;

vec3 worldUp(0.0f, 1.0f, 0.0f);

///////////////////////////////////////////////////////////////////////////////
// Models
///////////////////////////////////////////////////////////////////////////////
labhelper::Model* fighterModel = nullptr;
labhelper::Model* landingpadModel = nullptr;

mat4 roomModelMatrix;
mat4 landingPadModelMatrix;
mat4 fighterModelMatrix;

//like task1. add translation and rotation matrix for ship 
mat4 T(1.0f), R(1.0f); 

float shipSpeed = 50; 
// Particles
ParticleSystem particle_system(10000); 
mat4 particleRotationMatrix; 
GLuint explosionTexture; //particles texture

void loadShaders(bool is_reload)
{
	GLuint shader = labhelper::loadShaderProgram("../project/simple.vert", "../project/simple.frag",
	                                             is_reload);
	if(shader != 0)
	{
		simpleShaderProgram = shader;
	}

	shader = labhelper::loadShaderProgram("../project/fullscreenQuad.vert", "../project/background.frag",
	                                      is_reload);
	if(shader != 0)
	{
		backgroundProgram = shader;
	}

	shader = labhelper::loadShaderProgram("../project/shading.vert", "../project/shading.frag", is_reload);
	if(shader != 0)
	{
		shaderProgram = shader;
	}
	//shader = labhelper::loadShaderProgram("../project/basic.vert", "../project/basic.frag", false);
	//if (shader != 0)
	//{
	//	basicShaderProgram = shader;
	//}//

	shader = labhelper::loadShaderProgram("../project/particle.vert", "../project/particle.frag", false);
	if (shader != 0)
	{
		particleShaderProgram = shader;
	}
}



///////////////////////////////////////////////////////////////////////////////
/// This function is called once at the start of the program and never again
///////////////////////////////////////////////////////////////////////////////
void initialize()
{
	ENSURE_INITIALIZE_ONLY_ONCE();

	///////////////////////////////////////////////////////////////////////
	//		Load Shaders
	///////////////////////////////////////////////////////////////////////
	loadShaders(false);

	///////////////////////////////////////////////////////////////////////
	// Load models and set up model matrices
	///////////////////////////////////////////////////////////////////////
	fighterModel = labhelper::loadModelFromOBJ("../scenes/space-ship.obj");
	landingpadModel = labhelper::loadModelFromOBJ("../scenes/landingpad.obj");

	roomModelMatrix = mat4(1.0f);
	fighterModelMatrix = translate(15.0f * worldUp);
	landingPadModelMatrix = mat4(1.0f);

	///////////////////////////////////////////////////////////////////////
	// Load environment map
	///////////////////////////////////////////////////////////////////////
	const int roughnesses = 8;
	std::vector<std::string> filenames;
	for(int i = 0; i < roughnesses; i++)
		filenames.push_back("../scenes/envmaps/" + envmap_base_name + "_dl_" + std::to_string(i) + ".hdr");

	environmentMap = labhelper::loadHdrTexture("../scenes/envmaps/" + envmap_base_name + ".hdr");
	irradianceMap = labhelper::loadHdrTexture("../scenes/envmaps/" + envmap_base_name + "_irradiance.hdr");
	reflectionMap = labhelper::loadHdrMipmapTexture(filenames);


	///////////////////////////////////////////////////////////////////////
	// Setup Framebuffer for shadow map rendering
	///////////////////////////////////////////////////////////////////////
	shadowMapFB.resize(shadowMapResolution, shadowMapResolution);

	// shadow 
	glBindTexture(GL_TEXTURE_2D, shadowMapFB.depthBuffer);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	glEnable(GL_DEPTH_TEST); // enable Z-buffering
	glEnable(GL_CULL_FACE);  // enables backface culling

	// Particles
	particle_system.init_gpu_data();

	// Load Explosion (thrust) texture
	int expw, exph, expcomp;
	unsigned char* expimage = stbi_load("../scenes/textures/explosion.png", &expw, &exph, &expcomp, STBI_rgb_alpha);

	glGenTextures(1, &explosionTexture);
	glBindTexture(GL_TEXTURE_2D, explosionTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, expw, exph, 0, GL_RGBA, GL_UNSIGNED_BYTE, expimage);
	free(expimage);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glGenerateMipmap(GL_TEXTURE_2D);
	// Sets the type of filtering to be used on magnifying and
	// minifying the active texture. These are the nicest available options.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 16.0f);
}

void debugDrawLight(const glm::mat4& viewMatrix,
                    const glm::mat4& projectionMatrix,
                    const glm::vec3& worldSpaceLightPos)
{
	mat4 modelMatrix = glm::translate(worldSpaceLightPos);
	glUseProgram(simpleShaderProgram);
	labhelper::setUniformSlow(simpleShaderProgram, "modelViewProjectionMatrix",
	                          projectionMatrix * viewMatrix * modelMatrix);
	labhelper::setUniformSlow(simpleShaderProgram, "material_color", vec3(1, 1, 1));
	labhelper::debugDrawSphere();
}




void drawBackground(const mat4& viewMatrix, const mat4& projectionMatrix)
{
	glUseProgram(backgroundProgram);
	labhelper::setUniformSlow(backgroundProgram, "environment_multiplier", environment_multiplier);
	labhelper::setUniformSlow(backgroundProgram, "inv_PV", inverse(projectionMatrix * viewMatrix));
	labhelper::setUniformSlow(backgroundProgram, "camera_pos", cameraPosition);
	labhelper::drawFullScreenQuad();
}


///////////////////////////////////////////////////////////////////////////////
/// This function is used to draw the main objects on the scene
///////////////////////////////////////////////////////////////////////////////
void drawScene(GLuint currentShaderProgram,
               const mat4& viewMatrix,
               const mat4& projectionMatrix,
               const mat4& lightViewMatrix,
               const mat4& lightProjectionMatrix)
{
	glUseProgram(currentShaderProgram);
	// Light source
	vec4 viewSpaceLightPosition = viewMatrix * vec4(lightPosition, 1.0f);
	labhelper::setUniformSlow(currentShaderProgram, "point_light_color", point_light_color);
	labhelper::setUniformSlow(currentShaderProgram, "point_light_intensity_multiplier",
	                          point_light_intensity_multiplier);
	labhelper::setUniformSlow(currentShaderProgram, "viewSpaceLightPosition", vec3(viewSpaceLightPosition));
	labhelper::setUniformSlow(currentShaderProgram, "viewSpaceLightDir",
	                          normalize(vec3(viewMatrix * vec4(-lightPosition, 0.0f))));

	mat4 lightMatrix = translate(vec3(0.5f)) * scale(vec3(0.5f)) * lightProjectionMatrix * lightViewMatrix * inverse(viewMatrix);
	labhelper::setUniformSlow(currentShaderProgram, "lightMatrix", lightMatrix);

	// Environment
	labhelper::setUniformSlow(currentShaderProgram, "environment_multiplier", environment_multiplier);

	// camera
	labhelper::setUniformSlow(currentShaderProgram, "viewInverse", inverse(viewMatrix));

	// landing pad
	labhelper::setUniformSlow(currentShaderProgram, "modelViewProjectionMatrix",
	                          projectionMatrix * viewMatrix * landingPadModelMatrix);
	labhelper::setUniformSlow(currentShaderProgram, "modelViewMatrix", viewMatrix * landingPadModelMatrix);
	labhelper::setUniformSlow(currentShaderProgram, "normalMatrix",
	                          inverse(transpose(viewMatrix * landingPadModelMatrix)));

	labhelper::render(landingpadModel);

	// Fighter
	labhelper::setUniformSlow(currentShaderProgram, "modelViewProjectionMatrix",
	                          projectionMatrix * viewMatrix * fighterModelMatrix);
	labhelper::setUniformSlow(currentShaderProgram, "modelViewMatrix", viewMatrix * fighterModelMatrix);
	labhelper::setUniformSlow(currentShaderProgram, "normalMatrix",
	                          inverse(transpose(viewMatrix * fighterModelMatrix)));

	labhelper::render(fighterModel);
}


///////////////////////////////////////////////////////////////////////////////
/// This function will be called once per frame, so the code to set up
/// the scene for rendering should go here
///////////////////////////////////////////////////////////////////////////////
void display(void)
{
	///////////////////////////////////////////////////////////////////////////
	// Check if window size has changed and resize buffers as needed
	///////////////////////////////////////////////////////////////////////////
	{
		int w, h;
		SDL_GetWindowSize(g_window, &w, &h);
		if(w != windowWidth || h != windowHeight)
		{
			windowWidth = w;
			windowHeight = h;
		}
	}


	///////////////////////////////////////////////////////////////////////////
	// setup matrices
	///////////////////////////////////////////////////////////////////////////
	mat4 projMatrix = perspective(radians(45.0f), float(windowWidth) / float(windowHeight), 5.0f, 2000.0f);
	mat4 viewMatrix = lookAt(cameraPosition, cameraPosition + cameraDirection, worldUp);

    mat3 lightRot = rotate(radians(lightAzimuth), vec3(0, 1, 0)) * rotate(radians(lightZenith), vec3(0, 0, 1));
	lightPosition = lightRot * vec3(lightDistance, 0, 0);
	mat4 lightViewMatrix = lookAt(lightPosition, vec3(0.0f), worldUp);
	mat4 lightProjMatrix = perspective(radians(45.0f), 1.0f, 25.0f, 100.0f);

	///////////////////////////////////////////////////////////////////////////
	// Bind the environment map(s) to unused texture units
	///////////////////////////////////////////////////////////////////////////
	glActiveTexture(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, environmentMap);
	glActiveTexture(GL_TEXTURE7);
	glBindTexture(GL_TEXTURE_2D, irradianceMap);
	glActiveTexture(GL_TEXTURE8);
	glBindTexture(GL_TEXTURE_2D, reflectionMap);
	glActiveTexture(GL_TEXTURE0);


	
	// Set up shadow map parameters
///////////////////////////////////////////////////////////////////////////
	if (shadowMapFB.width != shadowMapResolution || shadowMapFB.height != shadowMapResolution) {
		shadowMapFB.resize(shadowMapResolution, shadowMapResolution);
	}

	// Control the clamp mode
	if (shadowMapClampMode == ClampMode::Edge) {
		glBindTexture(GL_TEXTURE_2D, shadowMapFB.depthBuffer);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	if (shadowMapClampMode == ClampMode::Border) {
		glBindTexture(GL_TEXTURE_2D, shadowMapFB.depthBuffer);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		vec4 border(shadowMapClampBorderShadowed ? 0.f : 1.f);
		glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, &border.x);
	}

	if (useHardwarePCF) {
		glBindTexture(GL_TEXTURE_2D, shadowMapFB.depthBuffer);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	}
	else {
		glBindTexture(GL_TEXTURE_2D, shadowMapFB.depthBuffer);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}

	// This line is to avoid some warnings from OpenGL for having the shadowmap attached to texture unit 0
	// when using a shader that samples from that texture with a sampler2D instead of a shadow sampler.
	// It is never actually sampled, but just having it set there generates the warning in some systems.
	glBindTexture(GL_TEXTURE_2D, 0);

	///////////////////////////////////////////////////////////////////////////
	// Draw Shadow Map
	///////////////////////////////////////////////////////////////////////////
	if (usePolygonOffset) {
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(polygonOffset_factor, polygonOffset_units);
	}

	// bind and clear frame buffer
	glBindFramebuffer(GL_FRAMEBUFFER, shadowMapFB.framebufferId);
	glViewport(0, 0, shadowMapFB.width, shadowMapFB.height);
	glClearColor(0.2f, 0.2f, 0.8f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glActiveTexture(GL_TEXTURE10);
	glBindTexture(GL_TEXTURE_2D, shadowMapFB.depthBuffer);
	drawScene(simpleShaderProgram, lightViewMatrix, lightProjMatrix, lightViewMatrix, lightProjMatrix);

	/*labhelper::Material& screen = landingpadModel->m_materials[8];
	screen.m_emission_texture.gl_id = shadowMapFB.colorTextureTargets[0];*/

	if (usePolygonOffset) {
		glDisable(GL_POLYGON_OFFSET_FILL);
	}

	///////////////////////////////////////////////////////////////////////////
	// Draw from camera
	///////////////////////////////////////////////////////////////////////////
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, windowWidth, windowHeight);
	glClearColor(0.2f, 0.2f, 0.8f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	drawBackground(viewMatrix, projMatrix);
	drawScene(shaderProgram, viewMatrix, projMatrix, lightViewMatrix, lightProjMatrix);
	debugDrawLight(viewMatrix, projMatrix, vec3(lightPosition));


	// Particles
	glEnable(GL_PROGRAM_POINT_SIZE);//allow dynamic sizing
	// Enable blending.
	glEnable(GL_BLEND);/////allow transparency
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glUseProgram(particleShaderProgram);// selects the particle shader 
	glBindTexture(GL_TEXTURE_2D, explosionTexture);///bind the loaded explosion
	glActiveTexture(GL_TEXTURE0);
	labhelper::setUniformSlow(particleShaderProgram, "P",
		projMatrix);//particle position

	labhelper::setUniformSlow(particleShaderProgram, "screen_x", float(windowWidth));//for scale the window
	labhelper::setUniformSlow(particleShaderProgram, "screen_y", float(windowHeight));

	
		const float theta = labhelper::uniform_randf(0.f, 2.f * M_PI); 
		const float u = labhelper::uniform_randf(0.95f, 1.f);  
		vec3 rand = vec3(u, sqrt(1.f - u * u) * cosf(theta), sqrt(1.f - u * u) * sinf(theta)); 
		//control the velocity and direction
		Particle p; 
		p.pos = fighterModelMatrix * vec4(10.0f, 1.0f, 0.0f, 1.0f);              //bind to ship location 
		p.velocity = particleRotationMatrix * vec4(rand, 1.0f) * 30.0f; 
		p.lifetime = 0.f; 
		p.life_length = 3.f;

		particle_system.spawn(p); 
	

	particle_system.process_particles(deltaTime);
	particle_system.submit_to_gpu(viewMatrix);
	glDisable(GL_BLEND);
	glDisable(GL_PROGRAM_POINT_SIZE);

}

///////////////////////////////////////////////////////////////////////////////
/// This function is used to update the scene according to user input
///////////////////////////////////////////////////////////////////////////////
bool handleEvents(void)
{
	// Allow ImGui to capture events.
	ImGuiIO& io = ImGui::GetIO();

	// check events (keyboard among other)
	SDL_Event event;
	bool quitEvent = false;
	while(SDL_PollEvent(&event))
	{
		ImGui_ImplSdlGL3_ProcessEvent(&event);

		if(event.type == SDL_QUIT || (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_ESCAPE))
		{
			quitEvent = true;
		}
		else if(event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_g)
		{
			showUI = !showUI;
		}
		else if(event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_PRINTSCREEN)
		{
			labhelper::saveScreenshot();
		}
		if(event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT
		   && (!showUI || !io.WantCaptureMouse))
		{
			g_isMouseDragging = true;
			int x;
			int y;
			SDL_GetMouseState(&x, &y);
			g_prevMouseCoords.x = x;
			g_prevMouseCoords.y = y;
		}

		if(!(SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT)))
		{
			g_isMouseDragging = false;
		}

		if(event.type == SDL_MOUSEMOTION && g_isMouseDragging && !io.WantCaptureMouse)
		{
			// More info at https://wiki.libsdl.org/SDL_MouseMotionEvent
			int delta_x = event.motion.x - g_prevMouseCoords.x;
			int delta_y = event.motion.y - g_prevMouseCoords.y;
			float rotationSpeed = 0.4f;
			mat4 yaw = rotate(rotationSpeed * deltaTime * -delta_x, worldUp);
			mat4 pitch = rotate(rotationSpeed * deltaTime * -delta_y,
			                    normalize(cross(cameraDirection, worldUp)));
			cameraDirection = vec3(pitch * yaw * vec4(cameraDirection, 0.0f));
			g_prevMouseCoords.x = event.motion.x;
			g_prevMouseCoords.y = event.motion.y;
		}
	}
	if (!io.WantCaptureKeyboard)
	{
		// check keyboard state (which keys are still pressed)
		const uint8_t* state = SDL_GetKeyboardState(nullptr);

		static bool was_shift_pressed = state[SDL_SCANCODE_LSHIFT];
		if (was_shift_pressed && !state[SDL_SCANCODE_LSHIFT])
		{
			cameraSpeed /= 5;
		}
		if (!was_shift_pressed && state[SDL_SCANCODE_LSHIFT])
		{
			cameraSpeed *= 5;
		}
		was_shift_pressed = state[SDL_SCANCODE_LSHIFT];


		vec3 cameraRight = cross(cameraDirection, worldUp);

		if (state[SDL_SCANCODE_W])
		{
			cameraPosition += cameraSpeed * deltaTime * cameraDirection;
		}
		if (state[SDL_SCANCODE_S])
		{
			cameraPosition -= cameraSpeed * deltaTime * cameraDirection;
		}
		if (state[SDL_SCANCODE_A])
		{
			cameraPosition -= cameraSpeed * deltaTime * cameraRight;
		}
		if (state[SDL_SCANCODE_D])
		{
			cameraPosition += cameraSpeed * deltaTime * cameraRight;
		}
		if (state[SDL_SCANCODE_Q])
		{
			cameraPosition -= cameraSpeed * deltaTime * worldUp;
		}
		if (state[SDL_SCANCODE_E])
		{
			cameraPosition += cameraSpeed * deltaTime * worldUp;
		}
		// control ship translation and rotation
		const float rotateSpeed = 4.f; 
		vec3 ship_forward = vec3(-1, 0, 0); 
		vec3 ship_up = vec3(0, 1, 0); 
		if (state[SDL_SCANCODE_UP])  
		{ 
			ship_forward = R * vec4(ship_forward, 0.0f); 
			T = translate(ship_forward * shipSpeed * deltaTime) * T; 
		} 
		if (state[SDL_SCANCODE_DOWN]) 
		{ 
			ship_forward = R * vec4(ship_forward, 0.0f);  
			T = translate(-ship_forward * shipSpeed * deltaTime) * T; 
		} 
		if (state[SDL_SCANCODE_LEFT]) 
		{ 
			R = rotate(rotateSpeed * deltaTime, vec3(0, 1, 0)) * R; 
		} 
		if (state[SDL_SCANCODE_RIGHT]) 
		{
			R = rotate(-rotateSpeed * deltaTime, vec3(0, 1, 0)) * R; 
		} 
		if (state[SDL_SCANCODE_SPACE]) 
		{ 
			T = translate(ship_up * shipSpeed * deltaTime) * T; 
		} 
		if (state[SDL_SCANCODE_X]) 
		{ 
			T = translate(-ship_up * shipSpeed * deltaTime) * T; 
		} 
		 
		fighterModelMatrix = T * R; 
	} 
	particleRotationMatrix = R; 
	const float light_rotation_speed = 90.f; 

	//light control
	if (animateLight) 
	{ 
		lightAzimuth += deltaTime * light_rotation_speed; 
		lightAzimuth = fmodf(lightAzimuth, 360.f); 
	} 

	return quitEvent;
}


///////////////////////////////////////////////////////////////////////////////
/// This function is to hold the general GUI logic
///////////////////////////////////////////////////////////////////////////////
void gui()
{



	ImGui::Checkbox("Animate light", &animateLight);
	ImGui::SliderFloat("Light Azimuth", &lightAzimuth, 0.0f, 360.0f);
	ImGui::SliderFloat("Light Zenith", &lightZenith, 0.0f, 90.0f);
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate,
	ImGui::GetIO().Framerate);
	
	//ImGui::SliderFloat("Particle Life Length", &particleLifeLength, 0.0f, 5.0f);  // life_length

	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate,
		ImGui::GetIO().Framerate);
	
}

int main(int argc, char* argv[])
{
	g_window = labhelper::init_window_SDL("OpenGL Project");

	initialize();

	bool stopRendering = false;
	auto startTime = std::chrono::system_clock::now();

	while(!stopRendering)
	{
		//update currentTime
		std::chrono::duration<float> timeSinceStart = std::chrono::system_clock::now() - startTime;
		previousTime = currentTime;
		currentTime = timeSinceStart.count();
		deltaTime = currentTime - previousTime;

		// Inform imgui of new frame
		ImGui_ImplSdlGL3_NewFrame(g_window);

		// check events (keyboard among other)
		stopRendering = handleEvents();

		// render to window
		display();

		// Render overlay GUI.
		if(showUI)
		{
			gui();
		}

		// Render the GUI.
		ImGui::Render();

		// Swap front and back buffer. This frame will now been displayed.
		SDL_GL_SwapWindow(g_window);
	}
	// Free Models
	labhelper::freeModel(fighterModel);
	labhelper::freeModel(landingpadModel);

	// Shut down everything. This includes the window and all other subsystems.
	labhelper::shutDown(g_window);
	return 0;
}
