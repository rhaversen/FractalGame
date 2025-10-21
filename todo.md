# TODO List

Move the player when changing power. Determine the direction the local space moves when changing power, and move the player by the same amount, making it so that the player moves along with the fractal.

Pass fractal type and power to the playercontroller, and update the speed limit based on fractal type and power.

Instead of the player moving with WASD, move and scale the fractal around a fixed player position. This will have the same effect, but will allow us to achieve higher detail levels in the fractal without floating point precision issues.

When changing fractal type, smoothly interpolate between the two fractal types over a short duration, instead of snapping instantly. Power already does this.

When changing power, limit the rate of change based on how close the player is to the fractal surface. This will make it impossible to clip through the fractal by changing power.

When reaching the end of the power scale, smoothly slow down the rate of change to zero, instead of reaching the limit abruptly.

Have custom power range for each fractal type.

We should interpolate between two different DE functions by making hybrid fractals and sliding the start/stop from the max de iter and 0, to gradually convert from one fractal to another. Then, we string all fractals together in a big list, and loop through them when changing fractal type.

Alternative fractal morphing mode: Automatically go in the sin and cos with power and fractal type, to create a continuous morphing fractal experience.

Alternative fractal movement mode: Let the player move, but when changing speed, we actually scale up the fractal and move the player in the local space, to achieve higher detail levels without floating point issues.
We do the same for morphing between fractal types and powers.
