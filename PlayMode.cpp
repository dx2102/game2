#include "PlayMode.hpp"

#include "TintProgram.hpp"
#include "DrawLines.hpp"
#include "DrawTris.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

#include <random>
#include <cmath>

constexpr float CELL_H = 0.175f;
constexpr float GRAVITY = -4.55f;
constexpr float MOVE_SPEED = 1.14f;
constexpr float ROCKET_POWER = 3.03f;
constexpr float ROCKET_F_MAX = 7.0f;
constexpr float FALL_MAX = -0.66f;
constexpr float FALL_MAX_DIVE = -5.95f;
constexpr float BOUNCE_FUEL = 10.0f;
constexpr float BOUNCE_MIN = 0.5f;
constexpr float FUEL_MAX = 100.0f;
constexpr float FUEL_BURN = 46.0f;
constexpr float FUEL_REGEN = 520.0f;
constexpr float ROBOT_S = 0.175f;
constexpr float PLAYER_HW = 0.061f;
constexpr float STEP_MAX = 0.035f;
constexpr float ORB_RESPAWN = 4.0f;
constexpr float BOTTOM = -2.45f;
constexpr float PAD_DROP = 0.88f;
constexpr float PAD_OMEGA = 8.0f;
constexpr float SPRING_H = 0.8f;
constexpr float ORB_R = 0.08f;
constexpr float CAM_PITCH = -0.74f;
constexpr float CAM_DIST = 1.4f;

//[柱高字符, 沿路格数, 蹦床]，'.' 是沟，'L' 左转
struct Seg { char h; int n; bool pad; };
static const Seg PATH[] = {
	{'8',1,0}, {'.',1,0}, {'8',1,0}, {'.',1,0}, {'8',1,0},
	{'.',6,0}, {'2',1,0}, {'.',1,0},
	{'3',1,0}, {'.',1,0}, {'3',1,0}, {'.',1,0}, {'4',1,0},
	{'.',1,0}, {'4',1,0},
	{'.',1,0}, {'2',1,1}, {'.',1,0}, {'6',1,0},
	{'.',1,0}, {'0',1,1}, {'.',1,0}, {'b',1,0},
	{'.',1,0}, {'0',1,1},
	{'L',0,0},
	{'.',1,0}, {'c',1,0}, {'.',1,0}, {'c',1,0},
	{'.',1,0}, {'0',1,1}, {'.',1,0}, {'0',1,1},
	{'.',1,0}, {'0',1,1}, {'.',1,0}, {'0',1,1},
	{'.',1,0}, {'k',1,0},
};

static glm::vec4 hex(uint32_t h, float a = 1.0f) {
	return glm::vec4(((h >> 16) & 255) / 255.0f, ((h >> 8) & 255) / 255.0f, (h & 255) / 255.0f, a);
}
static glm::vec4 hsl(float h, float s, float l) {
	auto f = [&](float n) {
		float k = std::fmod(n + h * 12.0f, 12.0f);
		float a = s * std::min(l, 1.0f - l);
		return l - a * std::max(-1.0f, std::min(std::min(k - 3.0f, 9.0f - k), 1.0f));
	};
	return glm::vec4(f(0), f(8), f(4), 1.0f);
}
static float frand() { static std::mt19937 mt(0x5eed); return std::uniform_real_distribution< float >(0.0f, 1.0f)(mt); }

struct Buffers {
	std::vector< std::pair< MeshBuffer const *, GLuint > > list;
	Buffers() {
		for (char const *name : {"cube.pnct", "robot1.pnct", "spring.pnct", "extras.pnct"}) {
			MeshBuffer const *b = new MeshBuffer(data_path(name));
			list.emplace_back(b, b->make_vao_for_program(tint_program->program));
		}
	}
	void find(std::string const &mesh_name, Mesh const *&mesh, GLuint &vao) const {
		for (auto const &[b, v] : list) {
			auto f = b->meshes.find(mesh_name);
			if (f != b->meshes.end()) { mesh = &f->second; vao = v; return; }
		}
		throw std::runtime_error("mesh '" + mesh_name + "' not found in any buffer");
	}
};
static Load< Buffers > buffers(LoadTagDefault);

Scene::Transform *PlayMode::add_drawable(Scene &s, std::string const &mesh_name, glm::vec4 const &tint, glm::vec4 **tint_out) {
	Mesh const *mesh; GLuint vao;
	buffers->find(mesh_name, mesh, vao);
	s.transforms.emplace_back();
	Scene::Transform *xf = &s.transforms.back();
	s.drawables.emplace_back(xf);
	Scene::Drawable &d = s.drawables.back();
	d.pipeline = tint_program_pipeline;
	d.pipeline.vao = vao;
	d.pipeline.type = mesh->type;
	d.pipeline.start = mesh->start;
	d.pipeline.count = mesh->count;
	tints.push_back(tint);
	glm::vec4 *t = &tints.back();
	d.pipeline.set_uniforms = [t]() { glUniform4fv(tint_program->TINT_vec4, 1, glm::value_ptr(*t)); };
	if (tint_out) *tint_out = t;
	return xf;
}

float PlayMode::top_at(int x, int y) const {
	auto f = heights.find(key(x, y));
	return f == heights.end() ? -std::numeric_limits< float >::infinity() : f->second;
}

PlayMode::PlayMode() {
	{ //铺关卡
		int cx = 0, cy = 0, dx = 1, dy = 0;
		for (Seg const &seg : PATH) {
			if (seg.h == 'L') { int t = dx; dx = -dy; dy = t; continue; }
			if (seg.h != '.') {
				float h = float(seg.h <= '9' ? seg.h - '0' : seg.h - 'a' + 10) * CELL_H;
				for (int i = 0; i < seg.n; ++i) {
					int x = cx + dx * i, y = cy + dy * i;
					heights[key(x, y)] = h;
					if (seg.pad) tramps.insert(key(x, y));
					solids.push_back({x, y, h, seg.pad});
				}
			}
			cx += dx * seg.n; cy += dy * seg.n;
		}
		goal = &solids.back();
		hint_zone = &solids[2];
	}

	for (Cell const &c : solids) {
		float top = c.pad ? c.top - PAD_DROP : c.top;
		int hh = int(std::round(c.top / CELL_H));
		glm::vec4 col = (&c == goal) ? hsl(0.11f, 0.85f, 0.55f) : hsl(0.56f - (hh - 3) * 0.045f, 0.46f, 0.66f);
		Scene::Transform *xf = add_drawable(scene, "Cube", col);
		xf->position = glm::vec3(c.x + 0.5f, c.y + 0.5f, (top + BOTTOM) * 0.5f);
		xf->scale = glm::vec3(1.0f, 1.0f, top - BOTTOM);
		if (!c.pad) continue;

		Scene::Transform *base = add_drawable(scene, "Cube", hex(0x39404a));
		base->position = glm::vec3(c.x + 0.5f, c.y + 0.5f, top + 0.025f);
		base->scale = glm::vec3(0.9f, 0.9f, 0.05f);

		Scene::Transform *spring = add_drawable(scene, "ring", hex(0xffffff));
		spring->position = glm::vec3(c.x + 0.5f, c.y + 0.5f, top + 0.05f);
		spring->scale = glm::vec3(0.25f, 0.25f, SPRING_H / 2.1f);

		Scene::Transform *board = add_drawable(scene, "board", hex(0xffffff));
		board->position = glm::vec3(c.x + 0.5f, c.y + 0.5f, c.top - 0.615f);
		board->scale = glm::vec3(0.9f, 0.9f, 0.3f);

		pads.push_back({c.x, c.y, c.top, spring, board, board->position.z});
		pad_of[key(c.x, c.y)] = &pads.back();
	}

	for (size_t i = pads.size() - 4; i < pads.size(); ++i) {
		Pad const &p = pads[i];
		Scene::Transform *xf = add_drawable(scene, "Orb", hex(0xffffff));
		float z = p.top + (7.0f + 4.0f * float(i - (pads.size() - 4))) * CELL_H;
		xf->position = glm::vec3(p.x + 0.5f, p.y + 0.5f, z);
		xf->scale = glm::vec3(ORB_R);
		orbs.push_back({xf, z});
	}

	robot = add_drawable(scene, "body", hex(0xffffff));
	for (char const *part : {"waist", "top"}) {
		scene.drawables.emplace_back(robot);
		Scene::Drawable &d = scene.drawables.back();
		Mesh const *mesh; GLuint vao;
		buffers->find(part, mesh, vao);
		d.pipeline = tint_program_pipeline;
		d.pipeline.vao = vao;
		d.pipeline.type = mesh->type; d.pipeline.start = mesh->start; d.pipeline.count = mesh->count;
		d.pipeline.set_uniforms = []() { glUniform4f(tint_program->TINT_vec4, 1.0f, 1.0f, 1.0f, 1.0f); };
	}

	ghost = add_drawable(glass, "Cylinder", hex(0x2b3138, 0.11f), &ghost_tint);
	ring = add_drawable(glass, "Cylinder", hex(0x2b3138, 0.34f), &ring_tint);

	scene.transforms.emplace_back();
	cam_xf = &scene.transforms.back();
	scene.cameras.emplace_back(cam_xf);
	camera = &scene.cameras.back();
	camera->fovy = glm::radians(58.0f);
	camera->near = 0.01f;

	checkpoint = glm::vec3(0.5f, 0.5f, 8.0f * CELL_H);
	respawn();
	cam_yaw = 0.0f;
}

PlayMode::~PlayMode() { }

void PlayMode::respawn() {
	pos = checkpoint + glm::vec3(0.0f, 0.0f, 0.35f);
	vel = glm::vec3(0.0f);
	riding = false;
	on_ground = false;
	squash = 0.0f;
	parts.clear();
	fuel = FUEL_MAX;
	if (won) { won = false; elapsed_time = 0.0f; }
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	if (evt.type != SDL_EVENT_KEY_DOWN && evt.type != SDL_EVENT_KEY_UP) return false;
	bool down = (evt.type == SDL_EVENT_KEY_DOWN);
	switch (evt.key.key) {
		case SDLK_W: k_w = down; return true;
		case SDLK_S: k_s = down; return true;
		case SDLK_A: k_a = down; return true;
		case SDLK_D: k_d = down; return true;
		case SDLK_SPACE: k_space = down; return true;
		case SDLK_M: case SDLK_LSHIFT: case SDLK_RSHIFT: k_glide = down; return true;
		case SDLK_Q: if (down && !evt.key.repeat) turns += 1; return true;
		case SDLK_E: if (down && !evt.key.repeat) turns -= 1; return true;
		case SDLK_R: if (down) want_respawn = true; return true;
		case SDLK_ESCAPE: if (down) Mode::set_current(nullptr); return true;
	}
	return false;
}

bool PlayMode::overlaps(glm::vec3 const &p) const {
	int x0 = int(std::floor(p.x - PLAYER_HW)), x1 = int(std::floor(p.x + PLAYER_HW));
	int y0 = int(std::floor(p.y - PLAYER_HW)), y1 = int(std::floor(p.y + PLAYER_HW));
	float feet = p.z + 1e-4f;
	for (int x = x0; x <= x1; ++x) for (int y = y0; y <= y1; ++y)
		if (feet < top_at(x, y)) return true;
	return false;
}

void PlayMode::move_and_collide(glm::vec3 d) {
	int steps = std::max(1, int(std::ceil(glm::length(d) / STEP_MAX)));
	d /= float(steps);
	float dz0 = d.z;
	for (int s = 0; s < steps; ++s) {
		pos.x += d.x;
		if (overlaps(pos)) { pos.x -= d.x; vel.x = 0.0f; d.x = 0.0f; }
		pos.z += d.z;
		if (overlaps(pos)) {
			pos.z -= d.z; vel.z = 0.0f; d.z = 0.0f;
			if (dz0 < 0.0f) {
				on_ground = true;
				float top = -std::numeric_limits< float >::infinity();
				for (int x = int(std::floor(pos.x - PLAYER_HW)); x <= int(std::floor(pos.x + PLAYER_HW)); ++x)
					for (int y = int(std::floor(pos.y - PLAYER_HW)); y <= int(std::floor(pos.y + PLAYER_HW)); ++y)
						top = std::max(top, top_at(x, y));
				pos.z = top;
			}
		}
		pos.y += d.y;
		if (overlaps(pos)) { pos.y -= d.y; vel.y = 0.0f; d.y = 0.0f; }
	}
}

void PlayMode::update(float dt) {
	if (want_respawn) { want_respawn = false; respawn(); }
	facing = ((facing + turns) % 4 + 4) % 4;
	turns = 0;
	if (!won) elapsed_time += dt;

	float yaw = facing * float(M_PI) * 0.5f;
	glm::vec2 fwd(std::cos(yaw), std::sin(yaw)), left(-fwd.y, fwd.x);
	glm::vec2 mv(0.0f);
	if (k_w) mv += fwd;
	if (k_s) mv -= fwd;
	if (k_a) mv += left;
	if (k_d) mv -= left;
	if (glm::length(mv) > 0.0f) mv = glm::normalize(mv);
	vel.x = mv.x * MOVE_SPEED;
	vel.y = mv.y * MOVE_SPEED;

	bool rocketing = false;
	if (k_space && !riding && fuel > 0.0f) {
		vel.z += std::min(ROCKET_F_MAX, ROCKET_POWER / std::max(vel.z, 0.01f)) * dt;
		fuel = std::max(0.0f, fuel - FUEL_BURN * dt);
		rocketing = true;
	}
	if (on_ground && !rocketing) fuel = std::min(FUEL_MAX, fuel + FUEL_REGEN * dt);

	if (riding) {
		int n = int(std::ceil(dt / 0.004f));
		float h = dt / n;
		for (int i = 0; i < n && riding; ++i) {
			ride.vz += PAD_OMEGA * PAD_OMEGA * ride.x * h;
			ride.x -= ride.vz * h;
			if (ride.vz > 0.0f && ride.x <= 0.0f) { vel.z = ride.launch; ride.pad->t = 0.0f; riding = false; }
		}
		if (riding) ride.pad->drop = ride.x;
	}

	bool was_ground = on_ground;
	if (!riding) {
		vel.z += GRAVITY * dt;
		float fmax = k_glide ? FALL_MAX : FALL_MAX_DIVE;
		if (vel.z < fmax) vel.z = fmax;
		on_ground = false;
		impact_vz = vel.z;
		move_and_collide(vel * dt);

		if (on_ground && !was_ground) {
			int gx = int(std::floor(pos.x)), gy = int(std::floor(pos.y));
			float impact = -impact_vz;
			if (tramps.count(key(gx, gy)) && impact > BOUNCE_MIN) {
				fuel = std::min(FUEL_MAX, fuel + BOUNCE_FUEL);
				on_ground = false;
				Pad *pad = pad_of[key(gx, gy)];
				pad->t = 9.0f;
				ride = {pad, 0.0f, -impact, impact};
				riding = true;
			} else {
				squash = 1.0f;
			}
		}
	}

	int gx = int(std::floor(pos.x)), gy = int(std::floor(pos.y));
	if (on_ground && !tramps.count(key(gx, gy))) checkpoint = pos;
	if (on_ground && !won && gx == goal->x && gy == goal->y) won = true;
	if (pos.z < BOTTOM + 0.35f) respawn();

	for (Orb &o : orbs) {
		if (o.cooldown > 0.0f) {
			o.cooldown -= dt;
			if (o.cooldown <= 0.0f) o.xf->scale = glm::vec3(ORB_R);
			continue;
		}
		o.xf->rotation = glm::normalize(o.xf->rotation * glm::angleAxis(dt * 1.8f, glm::vec3(0.0f, 0.0f, 1.0f)) * glm::angleAxis(dt * 1.1f, glm::vec3(1.0f, 0.0f, 0.0f)));
		o.xf->position.z = o.base_z + std::sin(elapsed_time * 2.2f + o.xf->position.x) * 0.03f;
		glm::vec3 d = o.xf->position - (pos + glm::vec3(0.0f, 0.0f, 0.06f));
		if (glm::dot(d, d) < 0.2f * 0.2f) {
			o.xf->scale = glm::vec3(0.0f);
			o.cooldown = ORB_RESPAWN;
			fuel = FUEL_MAX;
		}
	}

	for (Pad &p : pads) {
		if (p.t < 0.8f) {
			p.t += dt;
			float r = p.t / 0.8f;
			p.drop = r >= 1.0f ? 0.0f : -0.06f * std::exp(-3.0f * r) * std::sin(r * float(M_PI) * 3.0f);
		}
		p.spring->scale.z = std::max(0.05f, 1.0f - p.drop / SPRING_H) * (SPRING_H / 2.1f);
		p.board->position.z = p.board_z - p.drop;
	}

	squash *= std::pow(0.0008f, dt);
	float speed = glm::length(glm::vec2(vel.x, vel.y));
	float want_tilt = (rocketing ? -0.30f : 0.0f) + speed * 0.022f;
	robot_tilt += (want_tilt - robot_tilt) * std::min(1.0f, dt * 10.0f);
	robot->position = pos - glm::vec3(0.0f, 0.0f, riding ? ride.pad->drop : 0.0f);
	robot->scale = ROBOT_S * glm::vec3(1.0f + squash * 0.28f, 1.0f + squash * 0.28f, 1.0f - squash * 0.36f);
	robot->rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 0.0f, 1.0f)) * glm::angleAxis(robot_tilt, glm::vec3(0.0f, 1.0f, 0.0f));

	if (rocketing) {
		for (int i = 0; i < 5; ++i) {
			float a = frand() * 2.0f * float(M_PI), r = frand() * 0.06f;
			float hue = frand() < 0.7f ? std::fmod(frand() * 2.0f - 0.33f + 12.0f, 12.0f) / 12.0f : frand();
			glm::vec4 c = hsl(hue, 1.0f, 0.5f);
			parts.push_back({
				pos + glm::vec3(std::cos(a) * r, std::sin(a) * r, 0.01f + frand() * 0.02f),
				glm::vec3(std::cos(a) * (0.21f + frand() * 0.6f) + vel.x * 0.35f, std::sin(a) * (0.21f + frand() * 0.6f) + vel.y * 0.35f, -0.8f - frand() * 0.96f),
				0.0f, 0.32f + frand() * 0.36f, glm::vec3(c)});
		}
	}
	for (size_t i = 0; i < parts.size();) {
		Particle &p = parts[i];
		p.age += dt;
		if (p.age >= p.life) { parts[i] = parts.back(); parts.pop_back(); continue; }
		p.v.z -= 1.6f * dt;
		p.p += p.v * dt;
		++i;
	}

	{ //落点柱
		float t = top_at(gx, gy);
		for (Orb const &o : orbs)
			if (o.cooldown <= 0.0f && o.xf->position.z + ORB_R < pos.z && o.xf->position.z + ORB_R > t
				&& glm::length(glm::vec2(o.xf->position) - glm::vec2(pos)) < ORB_R + PLAYER_HW) t = o.xf->position.z + ORB_R;
		float h = pos.z - t;
		bool show = std::isfinite(t) && h > 0.01f;
		ghost->scale = show ? glm::vec3(0.148f, 0.148f, h) : glm::vec3(0.0f);
		ghost->position = glm::vec3(pos.x, pos.y, t + 0.012f + h * 0.5f);
		float f = std::min(1.0f, h / (6.0f * CELL_H));
		ring->scale = show ? glm::vec3(0.19f * (1.0f + f * 0.5f), 0.19f * (1.0f + f * 0.5f), 0.01f) : glm::vec3(0.0f);
		ring->position = glm::vec3(pos.x, pos.y, t + 0.012f);
		ring_tint->a = 0.34f - f * 0.16f;
	}

	{ //相机
		float target = yaw, d = std::remainder(target - cam_yaw, 2.0f * float(M_PI));
		cam_yaw += d * std::min(1.0f, dt * 4.5f);
		glm::vec3 look = pos + glm::vec3(0.0f, 0.0f, CELL_H);
		glm::vec3 want = look + glm::vec3(-std::cos(cam_yaw) * std::cos(CAM_PITCH) * CAM_DIST, -std::sin(cam_yaw) * std::cos(CAM_PITCH) * CAM_DIST, -std::sin(CAM_PITCH) * CAM_DIST);
		cam_pos = cam_snap ? want : glm::mix(cam_pos, want, std::min(1.0f, dt * 3.5f));
		cam_snap = false;
		cam_xf->position = cam_pos;
		cam_xf->rotation = glm::quatLookAt(glm::normalize(look - cam_pos), glm::vec3(0.0f, 0.0f, 1.0f));
	}
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	glClearDepth(1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	GL_ERRORS();

	scene.draw(*camera);

	glm::mat4 clip_from_world = camera->make_projection() * glm::mat4(cam_xf->make_local_from_world());
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);
	glass.draw(*camera);
	{
		DrawTris tris(clip_from_world);
		glm::mat4x3 cf = cam_xf->make_world_from_local();
		glm::vec3 right = glm::vec3(cf[0]) * 0.012f, up = glm::vec3(cf[1]) * 0.012f;
		for (Particle const &p : parts) {
			float k = (p.age / p.life) * (p.age / p.life);
			glm::vec3 c = glm::mix(p.c, glm::vec3(1.0f), k);
			glm::u8vec4 col(uint8_t(c.r * 255), uint8_t(c.g * 255), uint8_t(c.b * 255), 240);
			tris.quad(p.p - right - up, p.p + right - up, p.p + right + up, p.p - right + up, col);
		}
	}
	glDepthMask(GL_TRUE);
	glDisable(GL_DEPTH_TEST);

	{ //HUD
		float aspect = camera->aspect;
		glm::mat4 hud(1.0f / aspect, 0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 0.0f, 1.0f);
		const glm::u8vec4 BLACK(0, 0, 0, 255), WHITE(255, 255, 255, 235), GREEN(0x16, 0xa3, 0x4a, 255), ORANGE(0xea, 0x58, 0x0c, 255);
		constexpr float H = 0.045f, LINE = 0.075f, PAD = 0.04f;
		float x0 = -aspect + 0.04f, y1 = 1.0f - 0.04f;
		int gx = int(std::floor(pos.x)), gy = int(std::floor(pos.y));
		bool blink = on_ground && gx == hint_zone->x && gy == hint_zone->y && std::fmod(elapsed_time, 1.0f) >= 0.5f;
		char timer[32]; snprintf(timer, sizeof(timer), "%.1fs", elapsed_time);
		char fuel_s[32]; snprintf(fuel_s, sizeof(fuel_s), "%d%%", int(std::round(fuel)));
		{
			DrawTris tris(hud);
			tris.rect(glm::vec2(x0, y1 - 8 * LINE - 0.08f - PAD * 2), glm::vec2(x0 + 1.32f, y1), WHITE);
			tris.rect(glm::vec2(x0, -1.0f + 0.04f), glm::vec2(x0 + 1.0f, -1.0f + 0.04f + 0.24f), WHITE);
			float bx0 = x0 + PAD, bx1 = x0 + 1.0f - PAD, by0 = -1.0f + 0.04f + PAD, by1 = by0 + 0.05f;
			tris.rect(glm::vec2(bx0, by0), glm::vec2(bx0 + (bx1 - bx0) * fuel / FUEL_MAX, by1), fuel < 30.0f ? ORANGE : GREEN);
		}
		DrawLines lines(hud);
		auto text = [&](std::string const &s, float x, float y, float h, glm::u8vec4 const &c) {
			lines.draw_text(s, glm::vec3(x, y, 0.0f), glm::vec3(h, 0.0f, 0.0f), glm::vec3(0.0f, h, 0.0f), c);
		};
		auto box = [&](glm::vec2 a, glm::vec2 b) {
			lines.draw(glm::vec3(a.x, a.y, 0), glm::vec3(b.x, a.y, 0), BLACK); lines.draw(glm::vec3(b.x, a.y, 0), glm::vec3(b.x, b.y, 0), BLACK);
			lines.draw(glm::vec3(b.x, b.y, 0), glm::vec3(a.x, b.y, 0), BLACK); lines.draw(glm::vec3(a.x, b.y, 0), glm::vec3(a.x, a.y, 0), BLACK);
		};
		float y = y1 - PAD - 0.08f;
		text("ROCKET SWEEPER BOT", x0 + PAD, y, 0.07f, BLACK); y -= LINE + 0.01f;
		const char *rows[][2] = {{"W / S", "FORWARD / BACK"}, {"A / D", "STRAFE"}, {"Q / E", "TURN 90"}, {"SPACE", "HOLD = ROCKET"}, {"M / SHIFT", "HOLD = OPEN GLIDER"}, {"R", "RESPAWN"}};
		for (int i = 0; i < 6; ++i) {
			glm::u8vec4 c = (i == 4 && blink) ? glm::u8vec4(0, 0, 0, 40) : BLACK;
			text(rows[i][0], x0 + PAD, y, H, c);
			text(rows[i][1], x0 + PAD + 0.42f, y, H, c);
			y -= LINE;
		}
		lines.draw(glm::vec3(x0 + PAD, y + LINE - 0.02f, 0), glm::vec3(x0 + 1.32f - PAD, y + LINE - 0.02f, 0), BLACK);
		y -= 0.02f;
		text("GOAL: GOLD PILLAR", x0 + PAD, y, H, BLACK);
		text(timer, x0 + 1.32f - PAD - 0.25f, y, H, BLACK);
		box(glm::vec2(x0, y1 - 8 * LINE - 0.08f - PAD * 2), glm::vec2(x0 + 1.32f, y1));

		float fy = -1.0f + 0.04f;
		box(glm::vec2(x0, fy), glm::vec2(x0 + 1.0f, fy + 0.24f));
		text("ROCKET FUEL", x0 + PAD, fy + 0.24f - PAD - H, H, BLACK);
		text(fuel_s, x0 + 1.0f - PAD - 0.16f, fy + 0.24f - PAD - H, H, BLACK);
		box(glm::vec2(x0 + PAD, fy + PAD), glm::vec2(x0 + 1.0f - PAD, fy + PAD + 0.05f));

		if (won) text(std::string("GOAL REACHED - ") + timer + " - PRESS R TO RESTART", -0.95f, 0.3f, 0.07f, BLACK);
	}
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	GL_ERRORS();
}
