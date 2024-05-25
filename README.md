# Carpentaria

A game inspired by 
[Factorio](https://www.factorio.com) and 
[SpaceChem](https://www.zachtronics.com/spacechem)

## Differences and dogmas

- Most robot-programming games have a robot in a world, controlled by a program
  "in" the robot.  The world and the program are entirely separate.  SpaceChem
  is (almost) unique in that the world and the program are the same; every
  grid square may contain an atom, an instruction, or both.  This produces much
  richer mechanics as the program flow and movement through the world are
  not orthogonal.
  
- Factorio provides a compelling self-directed gameplay loop, in contrast to
  the tiny puzzles of SpaceChem, and a rich exploration of network behavior. 
  Yet the game consists essentially of desigining static networks; mobile units
  are a small and secondary part of the game; they are literally on rails, or
  float above the fray, interacting with nothing.
  
- There is a strong analogy between SpaceChem's waldos and an *accumulator
  machine*, a simple computer.  An accumulator machine holds a single value,
  and maintains an *instruction pointer* describing its location in a
  *program*.  A waldo holds an atom, and has a location and direction in the
  grid.  The accumulator machine may *load-immediate* a value from the program,
  or modify the held value by an instruction such as *increment*.  A waldo
  may *grab* an atom or *bond* a held atom.  A waldo can also *drop* an atom;
  it is less common for an accumulator machine to *store-immediate* and
  overwrite its code.
  
- There are also differences; waldos actually hold extended molecules, which
  may collide with each other.  Waldos operate in a two dimensional world and
  have a location and direction, and *branch* by conditionally changing
  direction.  Accumulator machines operate in a one dimensional world,
  moving forward by default, and *jumping* conditionally to new locations.
  
- Consider now a two-dimensional *stack machine*.  Like a forklift, it holds a
  stack of items, and can pick up itms to add to, or put down items from, this
  stack.  It drives forward, can turn left or right, and has *control
  flow* by conditionally turning.  Its state is described by a location and
  direction, together analagous to an instruction pointer, and the stack it
  holds.  If the forklift takes its instructions from signs painted on the
  warehouse floor--turn left here if carrying wood--it is executing a program.
  
- Thus, the game.  Like Factorio, our goal is to grow a factory by routing
  items between facilities.  Like SpaceChem, we carry these items with multiple
  vehicles that we control only indirectly by laying out sequences of
  instructions for them to execute.  Like Factorio, this system is, at least
  potentially, massive and parallel; we can aim to have thousands of machines
  running around a growing grid of millions of squares.  Like Lemmings, we
  will have to program while the executing, carefully blocking off new areas we 
  work on, or scrambling to keep ahead of a tide of machines.
  
- Function calls and addressable memory are notably absent this system.  They
  are not needed for Turning completeness, but their absence may be annoying.
  "Beware of the Turing tar-pit in which everything is possible but nothing of
  interest is easy."
  
- We consciously choose a stack machine model over a register machine model to
  keep the instruction set small.  We don't want to have to adjust multiple
  parameters on the symbols we slap down.  FORTH is a well-established stack-
  based language and we use some of its terminology to name these instructions.
  
- The world of objects and instructions, manipulated by these machines, is
  exactly analagous to the object system and bytecode of a conventional
  dynamically typed language, such as Lua or Python.  We can, in fact, directly
  expose these objects to a more conventional scripting language for internal
  and modding use;  we thus have an ecosystem of many first-class machines
  interacting via the shared memory "world".
   








