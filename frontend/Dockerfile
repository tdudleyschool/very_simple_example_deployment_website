FROM nginx:alpine

COPY index.html /usr/share/nginx/html/
COPY script.js /usr/share/nginx/html/
COPY styles.css /usr/share/nginx/html/


# Run nginx in foreground for Render
CMD ["nginx", "-g", "daemon off;"]

# Expose port 80 (Render will route traffic to this)
EXPOSE 80