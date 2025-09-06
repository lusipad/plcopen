/**
 * @file simple_axis_test.cpp
 * @brief Simple axis movement test - ASCII only
 * @version MVP-1.0
 * @date 2025-09-04
 */

#include <iostream>
#include <memory>

// Simple axis simulation for testing
namespace plc_runtime {
    namespace motion {
        class Axis {
        public:
            Axis(int id) : axis_id_(id), position_(0.0), target_(0.0), velocity_(0.0), is_moving_(false) {}
            
            void move_to(double target_position, double max_velocity = 10.0) {
                std::cout << "Axis " << axis_id_ << " moving from " << position_ 
                         << " to " << target_position << " at max velocity " << max_velocity << std::endl;
                target_ = target_position;
                velocity_ = max_velocity;
                is_moving_ = true;
            }
            
            void update() {
                if (is_moving_) {
                    double distance = target_ - position_;
                    if (std::abs(distance) < 0.1) {
                        position_ = target_;
                        velocity_ = 0.0;
                        is_moving_ = false;
                        std::cout << "Axis " << axis_id_ << " reached target position " << position_ << std::endl;
                    } else {
                        double step = (distance > 0) ? velocity_ * 0.1 : -velocity_ * 0.1;
                        position_ += step;
                        std::cout << "Axis " << axis_id_ << " at position " << position_ << std::endl;
                    }
                }
            }
            
            bool is_moving() const { return is_moving_; }
            double get_position() const { return position_; }
            
        private:
            int axis_id_;
            double position_;
            double target_;
            double velocity_;
            bool is_moving_;
        };
    }
}

int main() {
    std::cout << "=== Simple Axis Movement Test ===" << std::endl;
    
    try {
        // Create axis instance
        auto axis = std::make_unique<plc_runtime::motion::Axis>(1);
        
        std::cout << "Initial position: " << axis->get_position() << std::endl;
        
        // Test movement 1
        axis->move_to(50.0, 15.0);
        
        // Simulate movement
        int cycles = 0;
        while (axis->is_moving() && cycles < 20) {
            axis->update();
            cycles++;
        }
        
        std::cout << "Movement 1 completed in " << cycles << " cycles" << std::endl;
        
        // Test movement 2
        axis->move_to(-25.0, 12.0);
        
        cycles = 0;
        while (axis->is_moving() && cycles < 20) {
            axis->update();
            cycles++;
        }
        
        std::cout << "Movement 2 completed in " << cycles << " cycles" << std::endl;
        
        // Test movement 3
        axis->move_to(0.0, 8.0);
        
        cycles = 0;
        while (axis->is_moving() && cycles < 20) {
            axis->update();
            cycles++;
        }
        
        std::cout << "Movement 3 completed in " << cycles << " cycles" << std::endl;
        
        std::cout << "Final position: " << axis->get_position() << std::endl;
        std::cout << "Axis test completed successfully!" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Axis test exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception in axis test" << std::endl;
        return 1;
    }
}