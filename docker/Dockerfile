FROM ubuntu:latest
ENV DEBIAN_FRONTEND=noninteractive
USER root
COPY ./install_docker.sh ./
RUN chmod +x ./install_docker.sh && sh ./install_docker.sh
ENTRYPOINT ["/Metadbgwas/metadbgwas.sh"]
ENV PATH="/Metadbgwas/:${PATH}"
ENV OPENSSL_CONF=/dev/null
