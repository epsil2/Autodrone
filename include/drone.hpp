#pragma once

#include "ai_unit.hpp"
#include "utils.hpp"
#include <SFML/Graphics.hpp>
#include <fstream>
#include "smoke.hpp"



const std::vector<uint64_t> architecture = { 7, 9, 9, 4 };


struct MeanSlide
{
	uint32_t over;
	std::vector<float> values;
	uint32_t index;
	float sum;

	MeanSlide(uint32_t count)
		: over(count)
		, values(count, 0.0f)
		, index(0)
		, sum(0.0f)
	{}

	void addValue(float f)
	{
		if (index >= over) {
			sum -= values[index%over];
		}
		values[index%over] = f;
		sum += f;
		++index;
	}

	float getMean() const {
		return sum / float(over);
	}
};


struct Drone : public AiUnit
{
	struct Thruster
	{
	private:
		float max_power;
	public:
		float power_ratio;
		float angle;
		float target_angle;
		float angle_var_speed;
		float max_angle;
		float angle_ratio;

		MeanSlide power_mean;

		void setAngle(float ratio)
		{
			target_angle = max_angle * std::max(-1.0f, std::min(1.0f, ratio));
		}

		void setPower(float ratio)
		{
			power_ratio = std::max(0.0f, std::min(1.0f, ratio));
			power_mean.addValue(power_ratio);
		}

		float getPower() const
		{
			return power_ratio * max_power;
		}

		float getPowerMean() const
		{
			return power_mean.getMean();
		}

		float getMaxPower() const
		{
			return max_power;
		}

		void setMaxPower(float p)
		{
			max_power = p;
		}

		Thruster()
			: angle(0.0f)
			, target_angle(0.0f)
			, angle_var_speed(2.0f)
			, max_angle(0.5f * PI)
			, max_power(2000.0f)
			, power_mean(10)
		{}

		void update(float dt)
		{
			const float speed = angle_var_speed;
			angle += speed * (target_angle - angle) * dt;
			angle_ratio = angle / max_angle;
		}
	};

	Thruster left, right;
	float thruster_offset = 35.0f;
	float radius;
	sf::Vector2f position;
	sf::Vector2f velocity;
	float angle;
	float angular_velocity;
	std::list<Smoke> smokes;
	uint32_t generation;
	uint32_t collected;
	uint32_t index;
	float total_time;
	sf::Vector2f gravity;
	bool done;

	Drone()
		: AiUnit(architecture)
		, radius(20.0f)
		, position(0.0f, 0.0f)
		, collected(0)
		, total_time(0.0f)
		, done(false)
	{

	}

	void loadDNAFromFile(const std::string& filename)
	{
		std::ifstream infile(filename);
		float value;
		uint32_t i(0);
		while (infile >> value) {
			dna.set<float>(i, value);
			++i;
		}
		infile.close();
	}

	Drone(const sf::Vector2f& pos)
		: AiUnit(architecture)
		, radius(20.0f)
		, position(pos)
		, collected(0)
		, total_time(0.0f)
		, done(false)
	{
	}

	void reset()
	{
		velocity = sf::Vector2f(0.0f, 0.0f);
		angle = 0.0f;
		angular_velocity = 0.0f;
		left.power_ratio = 0.0f;
		left.angle = 0.0f;
		right.power_ratio = 0.0f;
		right.angle = 0.0f;

		fitness = 0.0f;
		alive = true;
	}

	static float cross(sf::Vector2f v1, sf::Vector2f v2)
	{
		return v1.x * v2.y - v1.y * v2.x;
	}

	static float dot(sf::Vector2f v1, sf::Vector2f v2)
	{
		return v1.x * v2.x + v1.y * v2.y;
	}

	sf::Vector2f getThrust() const
	{
		const float angle_left = angle - left.angle - HalfPI;
		sf::Vector2f thrust_left = left.getPower() * sf::Vector2f(cos(angle_left), sin(angle_left));
		const float angle_right = angle + right.angle - HalfPI;
		sf::Vector2f thrust_right = right.getPower() * sf::Vector2f(cos(angle_right), sin(angle_right));

		return thrust_left + thrust_right;
	}

	float getTorque() const
	{
		const float inertia_coef = 0.8f;
		const float angle_left =  - left.angle - HalfPI;
		const float left_torque = left.getPower() / thruster_offset * cross(sf::Vector2f(cos(angle_left), sin(angle_left)), sf::Vector2f(1.0f, 0.0f));

		const float angle_right = right.angle - HalfPI;
		const float right_torque = right.getPower() / thruster_offset * cross(sf::Vector2f(cos(angle_right), sin(angle_right)), sf::Vector2f(-1.0f, 0.0f));
		return (left_torque + right_torque) * inertia_coef;
	}

	void update(float dt, bool update_smoke)
	{
		left.update(dt);
		right.update(dt);
		// Integration
		velocity += (gravity + getThrust()) * dt;
		position += velocity * dt;
		angular_velocity += getTorque() * dt;
		angle += angular_velocity * dt;

		if (update_smoke) {
			const sf::Vector2f drone_dir(cos(angle), sin(angle));
			const float smoke_vert_offset = 80.0f;
			const float smoke_duration = 1.0f;
			const float smoke_speed_coef = 0.5f;
			if (left.getPowerMean() > 0.1f) {
				const float power_ratio = left.getPowerMean();
				const float power = power_ratio * left.getMaxPower();
				const float left_angle = angle - left.angle + HalfPI;
				const sf::Vector2f left_direction(cos(left_angle), sin(left_angle));
				const sf::Vector2f left_pos = position - (thruster_offset + 20.0f) * drone_dir + left_direction * smoke_vert_offset * power_ratio;

				smokes.push_back(Smoke(left_pos, left_direction, smoke_speed_coef * power, power_ratio, smoke_duration* power_ratio));
			}

			if (right.getPowerMean() > 0.1f) {
				const float power_ratio = right.getPowerMean();
				const float power = power_ratio * right.getMaxPower();

				const float right_angle = angle + right.angle + HalfPI;
				const sf::Vector2f right_direction(cos(right_angle), sin(right_angle));
				const sf::Vector2f right_pos = position + (thruster_offset + 20.0f) * drone_dir + right_direction * smoke_vert_offset * right.getPowerMean();

				smokes.push_back(Smoke(right_pos, right_direction, smoke_speed_coef * power, power_ratio, smoke_duration * power_ratio));
			}

			for (Smoke& s : smokes) {
				s.update(dt);
			}

			smokes.remove_if([this](const Smoke& s) { return s.done(); });
		}
	}

	float getNormalizedAngle() const
	{
		return getAngle(sf::Vector2f(cos(angle), sin(angle))) / PI;
	}

	void process(const std::vector<float>& outputs) override
	{
		left.setPower(0.5f * (outputs[0] + 1.0f));
		left.setAngle(2.0f * outputs[1]);
		right.setPower(0.5f * (outputs[2] + 1.0f));
		right.setAngle(2.0f * outputs[3]);
	}
};
