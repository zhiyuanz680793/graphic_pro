#include "ParticleSystem.h"
#include <algorithm>
#include <labhelper.h>

using namespace glm;

ParticleSystem::ParticleSystem(int capacity) : max_size(capacity)
{
	gl_data_temp_buffer.resize(max_size);
}

ParticleSystem::~ParticleSystem()//Destructor
{
}

void ParticleSystem::init_gpu_data()
{
	glGenVertexArrays(1, &gl_vao);
	glBindVertexArray(gl_vao);

	glGenBuffers(1, &gl_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, gl_buffer);
	glBufferData(GL_ARRAY_BUFFER, max_size * sizeof(vec4), nullptr, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 4, GL_FLOAT, false, 0, 0);
	glEnableVertexAttribArray(0);
}
//process particles
void ParticleSystem::process_particles(float dt)
{
	for (Particle& particle : particles) {

		particle.pos += particle.velocity * dt; // update position 
		particle.lifetime += dt;   //update lifetime
		//std::cout << "Particle Life: " << particle.lifetime << std::endl;
		//// adjust transparency (alpha)
		//particle.alpha = 1.0f - (particle.lifetime / particle.life_length);

		//if (particle.lifetime > particle.life_length) {
		//	particle.alpha = 0.0f;
		//}

	}
	// Kill dead particles
	// particles is a vector < structure of particles>
	// particles.size() visit and check all members of particles
	for (unsigned i = 0; i < particles.size(); ++i) {
		
		if (particles[i].lifetime > particles[i].life_length)
		{
			//particles[i].alpha = 0;
				kill(i);
		}
	}
}

void ParticleSystem::submit_to_gpu(const glm::mat4& viewMat)
{
	unsigned int num_active_particles = particles.size();

	//firstly bind all particles into buffer, then submit buffer to GPU
	gl_data_temp_buffer.clear();
	for (const Particle& particle : particles) {
		const glm::vec3 pos = glm::vec3(viewMat * glm::vec4(particle.pos, 1.0));// translate from World Coordinate  into View Coordinate

		//vec4 pos = viewMat * vec4(particle.pos, 1.0f); // translate from World Coordinate  into View Coordinate
		//wrong version 
		//gl_data_temp_buffer.push_back(pos);
		// now do clamp to normalize
		vec4 tmp = glm::vec4(pos, glm::clamp(particle.lifetime / particle.life_length, 0.f, 1.f));
		gl_data_temp_buffer.push_back(tmp);
	}

	// sort particles by z-value/depth, ensuring rendered in the correct order, from nearest to farest
	std::sort(gl_data_temp_buffer.begin(), std::next(gl_data_temp_buffer.begin(), num_active_particles),
		[](const vec4& lhs, const vec4& rhs) { return lhs.z < rhs.z; });

	glBindVertexArray(gl_vao);
	glBindBuffer(GL_ARRAY_BUFFER, gl_buffer);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vec4) * num_active_particles, gl_data_temp_buffer.data());//submit datra

	glDrawArrays(GL_POINTS, 0, num_active_particles);// rendering particles by using OpenGL draw commands
}
// if number of particles <maximum , add new particle
void ParticleSystem::spawn(Particle particle) {
	if (particles.size() < max_size) {
		particles.push_back(particle);
	}
};
// delete the paricles
void ParticleSystem::kill(int id) {
	std::swap(particles[id], particles[particles.size() - 1]);
	particles.pop_back();
};
