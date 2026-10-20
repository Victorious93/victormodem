import logging, uvicorn
from . import config
from .app import create_app

def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    cfg = config.load()
    uvicorn.run(create_app(cfg), host=cfg.host, port=cfg.port)

if __name__ == "__main__":
    main()
