// For more information about this file see https://dove.feathersjs.com/guides/cli/application.html
import { feathers } from '@feathersjs/feathers'
import configuration from '@feathersjs/configuration'
import { koa, rest, bodyParser, errorHandler, parseAuthentication, cors, serveStatic } from '@feathersjs/koa'
import koaBody from 'koa-body'

import { configurationValidator } from './configuration'
import type { Application } from './declarations'
import { logError } from './hooks/log-error'
import { services } from './services/index'

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

// Configure koa-body for file uploads and JSON parsing
app.use(koaBody({
  multipart: true,
  formidable: {
    maxFileSize: 100 * 1024 * 1024, // 100MB
    keepExtensions: true
  }
}))

// Remove the default bodyParser since koa-body handles it
// app.use(bodyParser())

// Configure services and transports
app.configure(rest())

app.configure(services)

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
