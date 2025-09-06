// For more information about this file see https://dove.feathersjs.com/guides/cli/application.html
import { feathers } from '@feathersjs/feathers'
import configuration from '@feathersjs/configuration'
import { koa, rest, errorHandler, parseAuthentication, cors, serveStatic } from '@feathersjs/koa'
import koaBody from 'koa-body'
import { createPlatesRouter } from './routes/plates'

import { configurationValidator } from './configuration'
import type { Application } from './declarations'
import { logError } from './hooks/log-error'
import { services } from './services/index'
import { logger } from './logger'

const app: Application = koa(feathers())

// Load our app configuration (see config/ folder)
app.configure(configuration(configurationValidator))

// Set up Koa middleware
app.use(cors())
// Reply with a small JSON payload at the root path instead of the default index.html
// This ensures GET / returns project info as JSON per the requested behavior.
app.use(async (ctx: any, next: any) => {
  if (ctx.path === '/' && ctx.method === 'GET') {
    ctx.status = 200
    // Use environment package metadata first, then fall back to known config values
    const name = process.env.npm_package_name ?? (app.get('public') as any)?.name ?? 'node-api'
    const version = process.env.npm_package_version ?? null
    const description = process.env.npm_package_description ?? null

    ctx.body = {
      name,
      version,
      description,
      status: 'ok'
    }
  } else {
    await next()
  }
})

app.use(serveStatic(app.get('public')))
app.use(errorHandler())
app.use(parseAuthentication())

// Configure koa-body for file uploads and JSON parsing BEFORE rest transport
app.use(koaBody({
  multipart: true,
  formidable: {
    maxFileSize: 100 * 1024 * 1024, // 100MB
    keepExtensions: true
  }
}))

// Custom middleware to attach file data to Feathers context
app.use(async (ctx, next) => {
  // Store the Koa context in the request for Feathers services to access
  if (ctx.request.files) {
    ctx.feathersFiles = ctx.request.files
  }
  await next()
})

// Configure services and transports AFTER body parsing
app.configure(rest())



// Remove the default bodyParser since koa-body handles it
// app.use(bodyParser())

app.configure(services)

// Add custom routes
const platesRouter = createPlatesRouter(app)
app.use(platesRouter.routes())
app.use(platesRouter.allowedMethods())

// Register hooks that run on all service methods
app.hooks({
  around: {
    all: [logError]
  },
  before: {},
  after: {},
  error: {}
})
// Register application setup and teardown hooks here
app.hooks({
  setup: [],
  teardown: []
})

export { app }
