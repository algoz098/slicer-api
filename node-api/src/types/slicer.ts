export interface CoreInput {
  threeMfPath: string
  profiles: { machine: any; process: any; filament: any }
  flavor: string
  units: 'mm'
  temperatures?: { bed?: number; nozzle?: number; chamber?: number; waitMode?: 'M190/M109' | 'M116' }
  motion?: { accel?: number; jerk?: { x?: number; y?: number; z?: number; e?: number }; travelAccel?: number; speedLimits?: Record<string, number> }
  options?: { retraction?: any; combing?: any; cooling?: any; supports?: any; adhesion?: any; plate?: number }
}

export interface LayerPlan {
  z: number
  perimeterPaths: any[]
  infillPaths: any[]
  travels: any[]
  retractions: any[]
  bridges: any[]
  speeds?: any
}

export interface ToolpathGraph { layers: LayerPlan[] }

export interface GCodeContext { flavor: string; units: 'mm'; eMode: 'absolute' | 'relative'; pre?: string; post?: string }

export interface SlicerCore {
  slice(input: CoreInput): Promise<ToolpathGraph>
}

export interface WriterFacade {
  toGcode(tp: ToolpathGraph, ctx: GCodeContext): string | NodeJS.ReadableStream
}

