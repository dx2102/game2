#include "Mode.hpp"
#include "Scene.hpp"

#include <glm/glm.hpp>

#include <deque>
#include <vector>
#include <unordered_map>
#include <unordered_set>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//---- 关卡 ----
	struct Cell { int x, y; float top; bool pad; };
	std::vector< Cell > solids;
	std::unordered_map< int64_t, float > heights;
	std::unordered_set< int64_t > tramps;
	static int64_t key(int x, int y) { return (int64_t(x) << 32) ^ (uint32_t(y)); }
	float top_at(int x, int y) const;
	Cell const *goal = nullptr;
	Cell const *hint_zone = nullptr;

	struct Pad {
		int x, y; float top;
		Scene::Transform *spring, *board;
		float board_z, t = 9.0f, drop = 0.0f;
	};
	std::deque< Pad > pads;
	std::unordered_map< int64_t, Pad * > pad_of;

	struct Orb { Scene::Transform *xf; float base_z, cooldown = 0.0f; };
	std::vector< Orb > orbs;

	//---- 玩家 ----
	glm::vec3 pos, vel;
	bool on_ground = false;
	float impact_vz = 0.0f;
	float fuel = 100.0f;
	int facing = 0;
	float squash = 0.0f;
	glm::vec3 checkpoint;
	float elapsed_time = 0.0f;
	bool won = false;
	struct Ride { Pad *pad; float x, vz, launch; };
	bool riding = false;
	Ride ride;

	bool overlaps(glm::vec3 const &p) const;
	void move_and_collide(glm::vec3 d);
	void respawn();

	//---- 输入 ----
	bool k_w = false, k_a = false, k_s = false, k_d = false, k_space = false, k_glide = false;
	int turns = 0;
	bool want_respawn = false;

	//---- 画面 ----
	Scene scene, glass;
	std::deque< glm::vec4 > tints;
	Scene::Transform *robot = nullptr, *ghost = nullptr, *ring = nullptr;
	glm::vec4 *ghost_tint = nullptr, *ring_tint = nullptr;
	Scene::Transform *cam_xf = nullptr;
	Scene::Camera *camera = nullptr;
	float cam_yaw = 0.0f;
	glm::vec3 cam_pos;
	bool cam_snap = true;
	float robot_tilt = 0.0f;

	struct Particle { glm::vec3 p, v; float age, life; glm::vec3 c; };
	std::vector< Particle > parts;

	Scene::Transform *add_drawable(Scene &s, std::string const &mesh_name, glm::vec4 const &tint, glm::vec4 **tint_out = nullptr);
};
