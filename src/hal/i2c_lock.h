// The I2C bus the capacitive touch shares with the S3's codec: the touch is
// read by a task of its own (ui.cpp, v0.103.5) while the codec is set up
// from the main task, and LovyanGFX's i2c isn't safe from two tasks at once
// - each side holds this lock around its I2C work. The lock is made by its
// first user (on the capacitive boards usually the touch task, not setup()).
#pragma once

void i2c_lock();
void i2c_unlock();
