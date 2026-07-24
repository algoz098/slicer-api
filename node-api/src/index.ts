import { app } from './app'
import { logger } from './logger'

const port = app.get('port')
const host = app.get('host')

process.on('unhandledRejection', reason => logger.error('Unhandled Rejection %O', reason))

const server = app.listen(port).then((srv: any) => {
  logger.info('Feathers app listening on http://%s:%d', host || '0.0.0.0', port)
  return srv
}).catch((err: any) => {
  logger.error('Failed to start server: %s', err?.message ?? String(err))
  process.exit(1)
})

const shutdown = async (signal: string) => {
  logger.info('Received %s — shutting down gracefully', signal)
  try {
    const srv = await server
    if (srv) await new Promise<void>((resolve) => srv.close(() => resolve()))
  } catch {}
  process.exit(0)
}
process.on('SIGTERM', () => shutdown('SIGTERM'))
process.on('SIGINT', () => shutdown('SIGINT'))
