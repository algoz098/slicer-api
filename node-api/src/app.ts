// For more information about this file see https://dove.feathersjs.com/guides/cli/application.html
import { feathers } from '@feathersjs/feathers'
import configuration from '@feathersjs/configuration'
import { koa, rest, errorHandler, parseAuthentication, cors, serveStatic } from '@feathersjs/koa'
import koaBody from 'koa-body'

import { configurationValidator } from './configuration'
import type { Application } from './declarations'
import { logError } from './hooks/log-error'
import { services } from './services/index'
import loadOrca from './orca'
import  {mediasPath} from './services/medias/medias.shared'

import fs from 'node:fs'
import * as path from 'node:path'

const app: Application = koa(feathers())

// Load our app configuration (see config/ folder)
app.configure(configuration(configurationValidator))

// Set up Koa middleware
app.use(cors())
app.use(koaBody({ multipart: true, formidable: { keepExtensions: true } }))

// Mapear arquivos do koa-body para params (ctx.feathers)
app.use(async (ctx, next) => {
  if (ctx.request && (ctx.request as any).files) {
    (ctx.feathers as any).files = (ctx.request as any).files
  }
  return next()
})


// add koa middleware for medias service
app.use(async (ctx, next) => {
  if (!ctx.path.includes(mediasPath) || ctx.method !== 'GET') {
    return next()
  }
  await next()
  const res = ctx.body.path

  // res is a path for a file
  ctx.set('Content-Type', 'application/octet-stream')
  ctx.set('Content-Disposition', `attachment; filename=${path.basename(res)}`)

  // stream the file back
  ctx.body = fs.createReadStream(res)
  ctx.status = 200
  ctx.respond = true
})

// Static files (homepage)
const publicDir = path.resolve(__dirname, '..', 'public')
if (fs.existsSync(publicDir)) {
  app.use(serveStatic(publicDir))
}



app.use(errorHandler())
app.use(parseAuthentication())
// Eagerly load the Orca addon at API startup and log the configuration
// eslint-disable-next-line @typescript-eslint/no-var-requires

loadOrca(app)

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
